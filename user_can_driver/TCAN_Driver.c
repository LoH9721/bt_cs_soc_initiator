/***************************************************************************//**
 * @file    TCAN_Driver.c
 * @brief   CAN 驱动公有接口 — 基于 TI TCAN4550 库
 * @note    21 个 API + 中断回调 + 轮询 + TCAN_Test
 ******************************************************************************/
#include <string.h>
#include <stdbool.h>
#include "sl_sleeptimer.h"
#include "sl_gpio.h"
#include "sl_status.h"
#include "TCAN_Driver.h"
#include "tcan4550_spi.h"
#include "tcan4550_driver.h"
#include "TCAN4x5x_SPI.h"
#include "TCAN4x5x_Reg.h"
#include "TCAN4550.h"
#include "user_log_console.h"
#include "user_can_common/RteSys.h"

/* ===================================================================
 *  常量
 * =================================================================== */
#define TCAN_RX_QUEUE_SIZE          32
#define TCAN_TX_TIMEOUT_MS          100
#define TCAN_TX_HW_BUF_COUNT        8

/* ===================================================================
 *  接收帧
 * =================================================================== */
typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;
} tcan_rx_frame_t;

/* ===================================================================
 *  模块状态
 * =================================================================== */
static tcan_rx_frame_t  s_rx_queue[TCAN_RX_QUEUE_SIZE];
static volatile uint8_t s_rx_head   = 0;
static volatile uint8_t s_rx_tail   = 0;
static volatile uint8_t s_rx_count  = 0;

static volatile uint32_t s_tx_mbox_busy_mask = 0;
static volatile uint8_t  s_last_tx_result    = 0;

static volatile uint8_t s_bus_off_flag    = 0;
static volatile uint8_t s_bus_off_counter = 0;
static volatile uint8_t s_no_ack_flag     = 0;
static volatile uint8_t s_no_ack_counter  = 0;

static volatile bool   s_interrupt_opened = false;
static volatile bool   s_initialized      = false;
static int32_t         s_nint_int_no      = -1;
static volatile uint64_t s_tx_last_tick   = 0;

static volatile uint32_t s_isr_call_count = 0;
static volatile uint32_t s_isr_rx_count   = 0;
static volatile uint32_t s_isr_tc_count   = 0;

/* ===================================================================
 *  辅助
 * =================================================================== */
static uint32_t tcan_get_tick_ms(void)
{
    return sl_sleeptimer_tick_to_ms((uint32_t)sl_sleeptimer_get_tick_count64());
}

static int tcan_find_free_tx_buffer(void)
{
    uint32_t busy = s_tx_mbox_busy_mask;
    for (int i = 0; i < TCAN_TX_HW_BUF_COUNT; i++) {
        if (!(busy & (1UL << i))) return i;
    }
    return -1;
}

/* ===================================================================
 *  中断回调
 * =================================================================== */
static void tcan_nint_callback(uint8_t int_no, void *context)
{
    (void)int_no; (void)context;
    s_isr_call_count++;

    // ---- 阶段1: 设备中断 (通过驱动层) ----
    uint32_t dev_ir = tcan4550_read_dev_ir();
    if (dev_ir & (1u << 3)) {  // SPIERR = bit 3
        tcan4550_clear_spi_err();
    }

    /* CAN 总线唤醒: 睡眠中(STANDBY) + 总线唤醒事件(CANINT/LWU) → 置唤醒标志
     * 注: STANDBY 下拿不到唤醒帧 ID, ID 级过滤由 CanNm 层 PassiveStartUp 完成 */
    if (RteSys_GetCanSleepFlag()
        && (dev_ir & (REG_BITS_DEVICE_IR_CANINT | REG_BITS_DEVICE_IR_LWU))) {
        RteSys_SetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG, true);
    }

    // 清除设备中断
    tcan4550_clear_dev_ir(dev_ir);

    // ---- 阶段2: MCAN 中断 ----
    bool rf0n, tc, bo, other;
    tcan4550_read_clear_interrupts(&rf0n, &tc, &bo, &other);

    if (bo) {
        s_bus_off_flag = 1;
        s_bus_off_counter++;
        tcan4550_leave_init_mode();
    }

    // ---- 阶段3: RX ----
    if (rf0n) {
        s_isr_rx_count++;
        int drained = 0;
        while (drained < 16) {
            uint32_t rx_id;
            uint8_t  rx_data[8];
            uint8_t blen = tcan4550_read_next_fifo(&rx_id, rx_data);
            if (blen == 0) break;

            if (s_rx_count < TCAN_RX_QUEUE_SIZE) {
                s_rx_queue[s_rx_head].id  = rx_id;
                s_rx_queue[s_rx_head].len = blen;
                memcpy(s_rx_queue[s_rx_head].data, rx_data, blen);
                s_rx_head = (uint8_t)((s_rx_head + 1) % TCAN_RX_QUEUE_SIZE);
                s_rx_count++;
            }
            drained++;
        }
    }

    // ---- 阶段4: TX 完成 ----
    if (tc) {
        s_isr_tc_count++;
        uint32_t txto = AHB_READ_32(REG_MCAN_TXBTO);
        s_tx_mbox_busy_mask &= ~txto;
        AHB_WRITE_32(REG_MCAN_TXBTO, txto);
        s_last_tx_result = 0;
    }
}

/* ===================================================================
 *  初始化与控制
 * =================================================================== */
void TCAN_Init(void)
{
    s_initialized = false;

    tcan4550_spi_init();
    tcan4550_gpio_init();
    /* 不在此处做硬件复位: RST 高有效, 脉冲会导致 VCCOUT 掉电→BG24 跟着重启死循环。
     * TCAN4550 上电即脱离复位态, 参考工程 car_tcan4550_init() 也无 hw_reset。 */

    uint16_t rev;
    if (!tcan4550_chip_init(&rev)) {
        USER_LOG_INFO("[TCAN] Init FAILED" USER_LOG_NL);
        return;
    }
    USER_LOG_INFO("[TCAN] Device Rev: 0x%04X" USER_LOG_NL, rev);

    // 注册 nINT — 只注册一次!
    // 注意: 唤醒路径每次 CanTransciever_WakeUp() 都会重跑 TCAN_Init(), 重复注册会覆盖原回调/中断号
    if (s_nint_int_no == SL_GPIO_INTERRUPT_UNAVAILABLE)
    {
        sl_gpio_t nint = { .port = TCAN_PIN_NINT_PORT, .pin = TCAN_PIN_NINT_PIN };
        sl_gpio_set_pin_mode(&nint, SL_GPIO_MODE_INPUT_PULL, 1);
        sl_status_t st = sl_gpio_configure_external_interrupt(&nint,
                               &s_nint_int_no, SL_GPIO_INTERRUPT_FALLING_EDGE,
                               tcan_nint_callback, NULL);
        USER_LOG_INFO("[TCAN] nINT reg: PC%u int_no=%ld status=0x%04lX" USER_LOG_NL,
                      (unsigned int)TCAN_PIN_NINT_PIN, (long)s_nint_int_no, (unsigned long)st);
    }

    // 复位状态
    s_rx_head = s_rx_tail = s_rx_count = 0;
    s_tx_mbox_busy_mask = 0;
    s_last_tx_result = 0;
    s_bus_off_flag = 0; s_bus_off_counter = 0;
    s_no_ack_flag = 0;  s_no_ack_counter = 0;

    s_initialized = true;
    USER_LOG_INFO("[TCAN] Init OK" USER_LOG_NL);
}

void TCAN_Enable(void)
{
    tcan4550_leave_init_mode();
}

void TCAN_Disable(void)
{
    tcan4550_enter_init_mode();
}

void TCAN_TransceiverOn(void)
{
    tcan4550_set_device_mode(TCAN4x5x_DEVICE_MODE_NORMAL);
}

void TCAN_TransceiverOff(void)
{
    tcan4550_set_device_mode(TCAN4x5x_DEVICE_MODE_STANDBY);
    USER_LOG_INFO("[TCAN] transceiver STANDBY, dev_mode=0x%02lX" USER_LOG_NL,
                  (unsigned long)(AHB_READ_32(REG_DEV_MODES_AND_PINS) & 0xC0));
    /* 期望 0x40=STANDBY; 若回读 0x80=NORMAL 说明 STANDBY 没写进去 */
}

void TCAN_OpenInterrupt(void)
{
    AHB_WRITE_32(REG_MCAN_ILE, REG_BITS_MCAN_ILE_EINT0);
    s_interrupt_opened = true;
}

void TCAN_CloseInterrupt(void)
{
    AHB_WRITE_32(REG_MCAN_ILE, 0);
    s_interrupt_opened = false;
}

/* ===================================================================
 *  发送管理
 * =================================================================== */
void TCAN_SendAFrame(uint16_t id, uint8_t* p_buff, uint8_t len)
{
    if (p_buff == NULL) return;
    if (len > 8) len = 8;

    int idx = tcan_find_free_tx_buffer();
    if (idx < 0) {
        s_last_tx_result = 1;
        return;
    }

    tcan4550_write_tx_and_send((uint8_t)idx, (uint32_t)id, p_buff, len);
    s_tx_mbox_busy_mask |= (1UL << idx);
    s_tx_last_tick = tcan_get_tick_ms();
    s_last_tx_result = 0;
}

uint8_t TCAN_GetSendResult(void)
{
    if (s_last_tx_result != 0) {
        uint32_t elapsed = tcan_get_tick_ms() - (uint32_t)s_tx_last_tick;
        if (elapsed > TCAN_TX_TIMEOUT_MS) {
            s_tx_mbox_busy_mask = 0;
            return 1;
        }
    }
    return s_last_tx_result;
}

void TCAN_ClearMailbox(void)
{
    tcan4550_cancel_all_tx();
    s_tx_mbox_busy_mask = 0;
    s_last_tx_result = 0;
}

/* ===================================================================
 *  接收状态管理
 * =================================================================== */
uint8_t TCAN_CheckIfReceive(void)
{
    return (s_rx_count > 0) ? 1 : 0;
}

void TCAN_ClearReceivedFlag(void)
{
    // RX FIFO ack 在 TI 库 ReadNextFIFO 内部完成
}

uint8_t TCAN_ReadReceivedFrame(uint16_t* p_id, uint8_t* p_buff, uint8_t* p_len)
{
    if (s_rx_count == 0) return 0;

    *p_id   = (uint16_t)s_rx_queue[s_rx_tail].id;
    *p_len  = s_rx_queue[s_rx_tail].len;
    memcpy(p_buff, s_rx_queue[s_rx_tail].data, s_rx_queue[s_rx_tail].len);

    s_rx_tail = (uint8_t)((s_rx_tail + 1) % TCAN_RX_QUEUE_SIZE);
    s_rx_count--;
    return 1;
}

/* ===================================================================
 *  Bus-Off / No-Ack 错误管理
 * =================================================================== */
uint8_t TCAN_GetBusOffFlag(void)
{
    if (tcan4550_read_psr() & 0x80) s_bus_off_flag = 1;
    return s_bus_off_flag;
}
uint8_t TCAN_GetBusOffCounter(void)     { return s_bus_off_counter; }
void TCAN_ClearBusOffFlag(void)
{
    AHB_WRITE_32(REG_MCAN_IR, REG_BITS_MCAN_IR_BO);
    s_bus_off_flag = 0;
    tcan4550_leave_init_mode();
}
void TCAN_ClearBusOffCounter(void)      { s_bus_off_counter = 0; }
uint8_t TCAN_GetNoAckFlag(void)         { return s_no_ack_flag; }
uint8_t TCAN_GetNoAckCounter(void)      { return s_no_ack_counter; }
void TCAN_ClearNoAckFlag(void)          { s_no_ack_flag = 0; }
void TCAN_ClearNoAckCounter(void)       { s_no_ack_counter = 0; }

/* ===================================================================
 *  主循环轮询
 * =================================================================== */
void TCAN_PollMain(void)
{
    uint32_t now = tcan_get_tick_ms();

    // ---- 0. 睡眠中: SPI 轮询 DEV_IR 检测 CAN 总线唤醒 ----
    //  轮询 CANINT/LWU 置位唤醒标志, 作为 nINT 中断检测的补充/保险
    if (RteSys_GetCanSleepFlag() == true)
    {
        uint32_t dev_ir = tcan4550_read_dev_ir();
        if (dev_ir != 0u)
        {
            tcan4550_clear_dev_ir(dev_ir);   /* 先清标志, 再观察是否复置 */
        }
        if (dev_ir & (REG_BITS_DEVICE_IR_CANINT | REG_BITS_DEVICE_IR_LWU))
        {
            RteSys_SetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG, true);
            USER_LOG_INFO("[DBG] sleep wakeup: dev_ir=0x%08lX" USER_LOG_NL, (unsigned long)dev_ir);
        }
        return;   /* 休眠中不处理下面逻辑, 保护寄存器不影响重新操作而变化 */
    }

    // ---- 1. TX 完成轮询 (ISR 可能因 BLE 中断优先级被延迟) ----
    {
        uint32_t txto = AHB_READ_32(REG_MCAN_TXBTO);
        if (txto != 0) {
            s_tx_mbox_busy_mask &= ~txto;
            s_last_tx_result = 0;
        }
    }

    // ---- 2. TX 超时 ----
    if (s_tx_mbox_busy_mask != 0) {
        if ((now - (uint32_t)s_tx_last_tick) > TCAN_TX_TIMEOUT_MS) {
            TCAN_ClearMailbox();
            s_last_tx_result = 1;
            s_no_ack_flag = 1;
            s_no_ack_counter++;
        }
    }

    // ---- 2. Bus-Off 检查 ----
    if (tcan4550_read_psr() & 0x80) s_bus_off_flag = 1;

    // ---- 3. RX 后备轮询 ----
    {
        int drained = 0;
        while (drained < 8) {
            uint32_t rx_id;
            uint8_t  rx_data[8];
            uint8_t blen = tcan4550_read_next_fifo(&rx_id, rx_data);
            if (blen == 0) break;

            if (s_rx_count < TCAN_RX_QUEUE_SIZE) {
                s_rx_queue[s_rx_head].id  = rx_id;
                s_rx_queue[s_rx_head].len = blen;
                memcpy(s_rx_queue[s_rx_head].data, rx_data, blen);
                s_rx_head = (uint8_t)((s_rx_head + 1) % TCAN_RX_QUEUE_SIZE);
                s_rx_count++;
            }
            drained++;
        }
    }
}

/* ===================================================================
 *  测试
 * =================================================================== */
void TCAN_Test(void)
{
    while (TCAN_CheckIfReceive()) {
        uint16_t rx_id; uint8_t rx_data[8]; uint8_t rx_len;
        TCAN_ReadReceivedFrame(&rx_id, rx_data, &rx_len);
    }
}

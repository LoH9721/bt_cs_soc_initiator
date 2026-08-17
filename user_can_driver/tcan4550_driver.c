/***************************************************************************//**
 * @file    tcan4550_driver.c
 * @brief   TCAN4550 芯片驱动核心 — 基于 TI TCAN4550 库
 ******************************************************************************/
#include <string.h>
#include "sl_sleeptimer.h"
#include "sl_gpio.h"
#include "tcan4550_driver.h"
#include "tcan4550_spi.h"
#include "TCAN4550.h"
#include "TCAN4x5x_SPI.h"
#include "TCAN4x5x_Reg.h"
#include "TCAN4x5x_Data_Structs.h"
#include "user_log_console.h"

/* ===================================================================
 *  接收白名单
 * =================================================================== */
const uint16_t g_tcan_rx_accept_ids[TCAN_RX_ACCEPT_COUNT] = {
    0x282u, 0x6DEu, 0x61Au, 0x217u
};

/* ===================================================================
 *  波特率参数
 * =================================================================== */
#define TCAN_NOM_PRESCALER      2
#define TCAN_NOM_TQ_BEFORE      32
#define TCAN_NOM_TQ_AFTER       8

/* ===================================================================
 *  毫秒级时间戳
 * =================================================================== */
static uint32_t tcan_get_tick_ms(void)
{
    return sl_sleeptimer_tick_to_ms((uint32_t)sl_sleeptimer_get_tick_count64());
}

/* ===================================================================
 *  GPIO 初始化 (RST, nINT)
 * =================================================================== */
void tcan4550_gpio_init(void)
{
    sl_gpio_t rst = { .port = TCAN_PIN_RST_PORT, .pin = TCAN_PIN_RST_PIN };
    /* RST 高有效复位；常态保持低 = 芯片正常运行（对齐参考工程 car_tcan4550_hal.c） */
    sl_gpio_set_pin_mode(&rst, SL_GPIO_MODE_PUSH_PULL, 1);
    sl_gpio_clear_pin(&rst);

    sl_gpio_t nint = { .port = TCAN_PIN_NINT_PORT, .pin = TCAN_PIN_NINT_PIN };
    sl_gpio_set_pin_mode(&nint, SL_GPIO_MODE_INPUT_PULL, 1);

    USER_LOG_INFO("[TCAN_DRV] GPIO init done: RST=PD%d, nINT=PC%d" USER_LOG_NL,
                  TCAN_PIN_RST_PIN, TCAN_PIN_NINT_PIN);
}

/* ===================================================================
 *  硬件复位
 * =================================================================== */
void tcan4550_hw_reset(void)
{
    sl_gpio_t rst = { .port = TCAN_PIN_RST_PORT, .pin = TCAN_PIN_RST_PIN };

    /* RST 高有效复位: 先拉高(assert)短暂脉冲 → 再拉低(release)等芯片启动 */
    sl_gpio_set_pin(&rst);
    USER_LOG_INFO("[TCAN_DRV] RST high (assert)..." USER_LOG_NL);
    {
        uint32_t start = tcan_get_tick_ms();
        while ((tcan_get_tick_ms() - start) < 1) {}  /* 1ms 脉冲足够, 避免长复位导致 VCCOUT 掉电 */
    }

    sl_gpio_clear_pin(&rst);
    USER_LOG_INFO("[TCAN_DRV] RST low (release), waiting chip boot..." USER_LOG_NL);
    {
        uint32_t start = tcan_get_tick_ms();
        while ((tcan_get_tick_ms() - start) < 5) {}
    }
    USER_LOG_INFO("[TCAN_DRV] Chip boot wait done" USER_LOG_NL);
}

/* ===================================================================
 *  芯片初始化 — 使用 TI 库函数, 与参考工程逻辑一致
 * =================================================================== */
bool tcan4550_chip_init(uint16_t* revision)
{
    USER_LOG_INFO("[TCAN_DRV] ---- chip_init start ----" USER_LOG_NL);

    // ---- 0. 读设备信息 ----
    *revision = TCAN4x5x_Device_ReadDeviceVersion();
    USER_LOG_INFO("[TCAN_DRV] Device Rev=0x%04X" USER_LOG_NL, *revision);

    // ---- 1. 清除 SPIERR ----
    TCAN4x5x_Device_ClearSPIERR();
    USER_LOG_INFO("[TCAN_DRV] SPIERR cleared" USER_LOG_NL);

    // ---- 2. 禁用设备中断 ----
    {
        TCAN4x5x_Device_Interrupt_Enable dev_ie = { 0 };
        dev_ie.CANINTEN = 1;   /* CAN 总线唤醒 → 拉低 nINT */
        dev_ie.LWUEN    = 0;   /* WAKE 引脚唤醒: 当前未用/未接, 保持 0 防误醒 */
        TCAN4x5x_Device_ConfigureInterruptEnable(&dev_ie);
    }

    // ---- 3. 清除 PWRON ----
    {
        TCAN4x5x_Device_Interrupts dev_ir = { 0 };
        TCAN4x5x_Device_ReadInterrupts(&dev_ir);
        if (dev_ir.PWRON) {
            TCAN4x5x_Device_ClearInterrupts(&dev_ir);
        }
        USER_LOG_INFO("[TCAN_DRV] Dev IRQ cleared" USER_LOG_NL);
    }

    // ---- 4. 时序配置 ----
    TCAN4x5x_MCAN_Nominal_Timing_Simple nom = { 0 };
    nom.NominalBitRatePrescaler = TCAN_NOM_PRESCALER;
    nom.NominalTqBeforeSamplePoint = TCAN_NOM_TQ_BEFORE;
    nom.NominalTqAfterSamplePoint = TCAN_NOM_TQ_AFTER;

    TCAN4x5x_MCAN_Data_Timing_Simple dtm = { 0 };
    dtm.DataBitRatePrescaler = TCAN_NOM_PRESCALER;
    dtm.DataTqBeforeSamplePoint = TCAN_NOM_TQ_BEFORE;
    dtm.DataTqAfterSamplePoint = TCAN_NOM_TQ_AFTER;

    // ---- 5. CCCR 配置 ----
    TCAN4x5x_MCAN_CCCR_Config cccr_cfg = { 0 };
    cccr_cfg.FDOE = 0;
    cccr_cfg.BRSE = 0;
    cccr_cfg.DAR  = 1;  // 禁用自动重传, 避免失败时反复重试卡住

    // ---- 6. 全局过滤器 ----
    TCAN4x5x_MCAN_Global_Filter_Configuration gfc = { 0 };
    gfc.RRFE = 1;
    gfc.RRFS = 1;
    gfc.ANFE = TCAN4x5x_GFC_REJECT;
    gfc.ANFS = TCAN4x5x_GFC_ACCEPT_INTO_RXFIFO0;

    // ---- 7. MRAM 配置 (与参考工程一致: 5 SID, 1 XID, 32 RX, 8 TX) ----
    TCAN4x5x_MRAM_Config mram = { 0 };
    mram.SIDNumElements = TCAN_RX_ACCEPT_COUNT;
    mram.XIDNumElements = 1;
    mram.Rx0NumElements = 32;
    mram.Rx0ElementSize = MRAM_8_Byte_Data;
    mram.Rx1NumElements = 0;
    mram.Rx1ElementSize = MRAM_8_Byte_Data;
    mram.RxBufNumElements = 0;
    mram.RxBufElementSize = MRAM_8_Byte_Data;
    mram.TxEventFIFONumElements = 0;
    mram.TxBufferNumElements = 8;
    mram.TxBufferElementSize = MRAM_8_Byte_Data;

    // ---- 8. 受保护寄存器写入 ----
    if (!TCAN4x5x_MCAN_EnableProtectedRegisters()) {
        USER_LOG_INFO("[TCAN_DRV] FAIL: EnableProtectedRegs" USER_LOG_NL);
        return false;
    }
    USER_LOG_INFO("[TCAN_DRV] Protected regs enabled" USER_LOG_NL);

    TCAN4x5x_MCAN_ConfigureCCCRRegister(&cccr_cfg);
    TCAN4x5x_MCAN_ConfigureGlobalFilter(&gfc);
    TCAN4x5x_MCAN_ConfigureNominalTiming_Simple(&nom);
    (void)TCAN4x5x_MCAN_ConfigureDataTiming_Simple(&dtm);
    USER_LOG_INFO("[TCAN_DRV] CCCR/GFC/NBTP/DBTP configured" USER_LOG_NL);

    TCAN4x5x_MRAM_Clear();
    USER_LOG_INFO("[TCAN_DRV] MRAM cleared" USER_LOG_NL);

    TCAN4x5x_MRAM_Configure(&mram);
    USER_LOG_INFO("[TCAN_DRV] MRAM layout configured" USER_LOG_NL);

    // ---- 9. SID 过滤器: 4 个 APP 精确匹配 + 1 个 NM 范围 ----
    for (uint8_t i = 0; i < 4; i++) {
        TCAN4x5x_MCAN_SID_Filter sid = { 0 };
        sid.SFEC = TCAN4x5x_SID_SFEC_STORERX0;
        sid.SFT = TCAN4x5x_SID_SFT_CLASSIC;
        sid.SFID1 = (uint16_t)(g_tcan_rx_accept_ids[i] & 0x7FFu);
        sid.SFID2 = 0x7FFu;
        (void)TCAN4x5x_MCAN_WriteSIDFilter(i, &sid);
    }
    /* NM range 0x400-0x4FF: RANGE 模式, 所有 NM 节点都可唤醒本机 */
    {
        TCAN4x5x_MCAN_SID_Filter nm = { 0 };
        nm.SFEC = TCAN4x5x_SID_SFEC_STORERX0;
        nm.SFT  = TCAN4x5x_SID_SFT_RANGE;
        nm.SFID1 = 0x400u;
        nm.SFID2 = 0x4FFu;
        (void)TCAN4x5x_MCAN_WriteSIDFilter(4, &nm);
    }

    // ---- 10. XID 过滤器 (禁用) ----
    {
        TCAN4x5x_MCAN_XID_Filter xid = { 0 };
        xid.EFT = TCAN4x5x_XID_EFT_CLASSIC;
        xid.EFEC = TCAN4x5x_XID_EFEC_DISABLED;
        xid.EFID1 = 0;
        xid.EFID2 = 0;
        TCAN4x5x_MCAN_WriteXIDFilter(0, &xid);
    }
    USER_LOG_INFO("[TCAN_DRV] SID/XID filters written" USER_LOG_NL);

    // ---- 11. 禁用受保护寄存器 ----
    TCAN4x5x_MCAN_DisableProtectedRegisters();

    // ---- 12. MCAN 中断使能 ----
    {
        TCAN4x5x_MCAN_Interrupt_Enable mcan_ie = { 0 };
        mcan_ie.RF0NE = 1;
        mcan_ie.RF0LE = 1;
        mcan_ie.TCE = 1;
        mcan_ie.BOE = 1;
        mcan_ie.MRAFE = 1;
        mcan_ie.EWE = 1;
        mcan_ie.EPE = 1;
        mcan_ie.ELOE = 1;
        TCAN4x5x_MCAN_ConfigureInterruptEnable(&mcan_ie);
    }
    AHB_WRITE_32(REG_MCAN_TXBTIE, (uint32_t)((1UL << 8) - 1UL));  // 8 TX buf
    AHB_WRITE_32(REG_MCAN_ILE, REG_BITS_MCAN_ILE_EINT0);
    USER_LOG_INFO("[TCAN_DRV] MCAN IRQ enabled" USER_LOG_NL);

    // ---- 13. 设备配置 (匹配参考工程) ----
    {
        TCAN4x5x_DEV_CONFIG dev;
        memset(&dev, 0, sizeof(dev));
        dev.SWE_DIS = 1;  /* 禁用 4min 睡眠唤醒错误定时器, 防止异常自动进入睡眠 */
        dev.DEVICE_RESET = 0;
        dev.WD_EN = 0;
        dev.nWKRQ_CONFIG = 0;
        dev.INH_DIS = 0;
        dev.GPIO1_GPO_CONFIG = TCAN4x5x_DEV_CONFIG_GPO1_MCAN_INT1;
        dev.FAIL_SAFE_EN = 0;
        dev.GPIO1_CONFIG = TCAN4x5x_DEV_CONFIG_GPIO1_CONFIG_GPO;
        dev.WD_ACTION = TCAN4x5x_DEV_CONFIG_WDT_ACTION_nINT;
        dev.WD_BIT_RESET = 0;
        dev.nWKRQ_VOLTAGE = 0;
        dev.GPO2_CONFIG = TCAN4x5x_DEV_CONFIG_GPO2_NO_ACTION;
        dev.CLK_REF = 1;
        dev.WAKE_CONFIG = TCAN4x5x_DEV_CONFIG_WAKE_BOTH_EDGES;
        (void)TCAN4x5x_Device_Configure(&dev);
    }

    // ---- 14. 设置 NORMAL 模式, 清除中断 ----
    TCAN4x5x_Device_SetMode(TCAN4x5x_DEVICE_MODE_NORMAL);
    TCAN4x5x_MCAN_ClearInterruptsAll();
    USER_LOG_INFO("[TCAN_DRV] Device NORMAL mode, IRQs cleared" USER_LOG_NL);

    // ---- 15. 最终 CCCR 检查 ----
    {
        uint32_t cccr = AHB_READ_32(REG_MCAN_CCCR);
        USER_LOG_INFO("[TCAN_DRV] Final CCCR=0x%08lX (INIT=%d)" USER_LOG_NL,
                      (unsigned long)cccr, (int)(cccr & REG_BITS_MCAN_CCCR_INIT));
    }

    USER_LOG_INFO("[TCAN_DRV] ---- chip_init OK ----" USER_LOG_NL);
    return true;
}

/* ===================================================================
 *  简单控制
 * =================================================================== */
void tcan4550_leave_init_mode(void)
{
    uint32_t cccr = AHB_READ_32(REG_MCAN_CCCR);
    cccr &= ~(uint32_t)REG_BITS_MCAN_CCCR_INIT;
    AHB_WRITE_32(REG_MCAN_CCCR, cccr);
}

void tcan4550_enter_init_mode(void)
{
    uint32_t cccr = AHB_READ_32(REG_MCAN_CCCR);
    cccr |= REG_BITS_MCAN_CCCR_INIT;
    AHB_WRITE_32(REG_MCAN_CCCR, cccr);
}

void tcan4550_set_device_mode(uint8_t mode)
{
    (void)TCAN4x5x_Device_SetMode((TCAN4x5x_Device_Mode_Enum)mode);
}

/* ===================================================================
 *  中断
 * =================================================================== */
uint32_t tcan4550_read_mcan_ir(void)      { return AHB_READ_32(REG_MCAN_IR); }
void tcan4550_clear_mcan_ir(uint32_t f)   { AHB_WRITE_32(REG_MCAN_IR, f); }
uint32_t tcan4550_read_dev_ir(void)       { return AHB_READ_32(REG_DEV_IR); }
void tcan4550_clear_dev_ir(uint32_t f)    { AHB_WRITE_32(REG_DEV_IR, f); }
void tcan4550_clear_spi_err(void)         { TCAN4x5x_Device_ClearSPIERR(); }
uint32_t tcan4550_read_txbrp(void)        { return AHB_READ_32(REG_MCAN_TXBRP); }
uint32_t tcan4550_read_txbto(void)        { return AHB_READ_32(REG_MCAN_TXBTO); }
uint32_t tcan4550_read_rxf0s(void)        { return AHB_READ_32(REG_MCAN_RXF0S); }
uint32_t tcan4550_read_psr(void)          { return AHB_READ_32(REG_MCAN_PSR); }
uint32_t tcan4550_read_ecr(void)          { return AHB_READ_32(REG_MCAN_ECR); }

/* ===================================================================
 *  RX: 使用 TI 库 ReadNextFIFO (内部处理 FIFO 读取 + ack)
 * =================================================================== */
uint8_t tcan4550_read_next_fifo(uint32_t* out_id, uint8_t* out_data)
{
    TCAN4x5x_MCAN_RX_Header rh;
    uint8_t pl[64];
    uint8_t n = TCAN4x5x_MCAN_ReadNextFIFO(RXFIFO0, &rh, pl);
    if (n == 0) return 0;

    // 跳过非标准数据帧
    if (rh.XTD || rh.RTR || rh.FDF) return n;

    uint8_t blen = TCAN4x5x_MCAN_DLCtoBytes((uint8_t)rh.DLC);
    if (blen > 8) blen = 8;

    if (out_id)  *out_id = rh.ID & 0x7FFu;
    if (out_data) memcpy(out_data, pl, blen);
    return blen;
}

/* ===================================================================
 *  TX: 使用 TI 库 WriteTXBuffer + TransmitBufferContents
 * =================================================================== */
void tcan4550_write_tx_and_send(uint8_t buf_index, uint32_t id,
                                 const uint8_t* data, uint8_t len)
{
    TCAN4x5x_MCAN_TX_Header h;
    memset(&h, 0, sizeof(h));
    h.ID = id & 0x7FFu;
    h.DLC = len;
    h.FDF = 0;
    h.BRS = 0;
    h.XTD = 0;
    h.RTR = 0;
    h.ESI = 0;
    h.EFC = 0;
    h.MM = 0;

    (void)TCAN4x5x_MCAN_WriteTXBuffer(buf_index, &h, data);
    (void)TCAN4x5x_MCAN_TransmitBufferContents(buf_index);
}

void tcan4550_cancel_all_tx(void)
{
    AHB_WRITE_32(REG_MCAN_TXBCR, 0xFFFFFFFF);
}

/* ===================================================================
 *  中断读取与清除 (使用 TI 库结构体)
 * =================================================================== */
void tcan4550_read_clear_interrupts(bool* rf0n, bool* tc, bool* bo, bool* other)
{
    TCAN4x5x_MCAN_Interrupts ir;
    TCAN4x5x_MCAN_ReadInterrupts(&ir);

    *rf0n  = ir.RF0N;
    *tc    = ir.TC;
    *bo    = ir.BO;
    *other = ir.RF0L || ir.MRAF || ir.EW || ir.EP || ir.ELO;

    // 清除已处理的中断
    TCAN4x5x_MCAN_Interrupts cl = { 0 };
    if (ir.RF0N) cl.RF0N = 1;
    if (ir.TC)   cl.TC   = 1;
    if (ir.BO)   cl.BO   = 1;
    if (ir.MRAF) cl.MRAF = 1;
    if (ir.RF0L) cl.RF0L = 1;
    if (ir.EW)   cl.EW   = 1;
    if (ir.EP)   cl.EP   = 1;
    if (ir.ELO)  cl.ELO  = 1;
    if (cl.word != 0u) {
        TCAN4x5x_MCAN_ClearInterrupts(&cl);
    }
}

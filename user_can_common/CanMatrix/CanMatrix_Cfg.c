/*******************************************************
 * Name    :CanMatrix_Cfg.c
 * Function:BLE APP 报文实例 + handler + 协议层 + RTE
 * Note    :CAN ID 严格按 DBC test_car.dbc
 *         TX: 0x2BE (BLE_KEY_INFO, 100ms)
 *         RX: 0x282 (VIU_BCMStsInf1), 0x6DE (BLE_TEST_INFO),
 *             0x61A (VIU_B_VINInfo),  0x217 (VIU_B_VIUCrlInfo2)
 *******************************************************/
#include "CanMatrix_Cfg.h"
#include "user_log_console.h"
#include <string.h>
#include "../../user_vehicle_state.h"

/*-------------handler 状态常量-----------------------*/
#define RX_STATE_INIT    0
#define RX_STATE_NORMAL  1
#define RX_STATE_LOST    2
#define TX_STATE_CHECK   0
#define TX_STATE_UPDATE  1
#define TX_STATE_CLEAR   2

/*============= 报文实例 ==============================*/
static CanMatrix_Tx2BEMsg_Type  g_Tx2BEMsg;
static CanMatrix_Rx282Msg_Type  g_Rx282Msg;
static CanMatrix_Rx6DEMsg_Type  g_Rx6DEMsg;
static CanMatrix_Rx61AMsg_Type  g_Rx61AMsg;
static CanMatrix_Rx217Msg_Type  g_Rx217Msg;
static CanMatrix_VinReasm_Type  g_VinReasm;

/*============= TX 命令脉冲锁存 (3 帧 @ 100ms) =======*/
#define TX2BE_PULSE_FRAMES  3U
static struct {
    uint8_t phone_cmd_latched,  phone_cmd_frames;
    uint8_t phone_lock_latched, phone_lock_frames;
    uint8_t key_cmd_latched,    key_cmd_frames;
    uint8_t key_lock_latched,   key_lock_frames;
} g_pulse;
static uint8_t g_2BECounter = 0;

/*============= CRC-8 SAE J1850 (量产参考: Communi CRC8_SAEJ1850_LOOKUP) =============*/
static uint8_t Crc8(const uint8_t* buf, uint8_t len)
{
    uint8_t i, j;
    uint8_t u8_poly;
    uint8_t u8_crc8;

    u8_crc8 = 0xFF;
    u8_poly = 0x1D;

    for (i = 0; i < len; i++)
    {
        u8_crc8 ^= buf[i];
        for (j = 0; j < 8; j++)
        {
            if ((u8_crc8 & 0x80) != 0)
            {
                u8_crc8 <<= 1;
                u8_crc8  ^= u8_poly;
            }
            else
            {
                u8_crc8 <<= 1;
            }
        }
    }
    u8_crc8 ^= (uint8_t)0xFF;
    return u8_crc8;
}

/*============= 原始指针 (供调度器 p_buff 用) ==========*/
uint8_t* CanMatrix_GetRxRaw282(void) { return g_Rx282Msg.data; }
uint8_t* CanMatrix_GetRxRaw6DE(void) { return g_Rx6DEMsg.data; }
uint8_t* CanMatrix_GetRxRaw61A(void) { return g_Rx61AMsg.data; }
uint8_t* CanMatrix_GetRxRaw217(void) { return g_Rx217Msg.data; }
uint8_t* CanMatrix_GetTxRaw2BE(void) { return g_Tx2BEMsg.data; }

/*============= RTE 数组 =============================*/
static uint8_t s_RteSig[RTE_CAN_SIG_NUM];

void user_can_rte_write_canSig(RTE_CAN_ID id, uint8_t sig)
{ if (id < RTE_CAN_SIG_NUM) s_RteSig[id] = sig; }
uint8_t user_can_rte_read_canSig(RTE_CAN_ID id)
{ return (id < RTE_CAN_SIG_NUM) ? s_RteSig[id] : 0U; }

/*============= 脉冲逻辑 ==============================*/
static void pulse_capture(void)
{
    uint8_t v;
    v = user_can_rte_read_canSig(RTE_2BE_Phone_Cmd);
    if (v && !g_pulse.phone_cmd_frames)   { g_pulse.phone_cmd_latched=v; g_pulse.phone_cmd_frames=TX2BE_PULSE_FRAMES; }
    v = user_can_rte_read_canSig(RTE_2BE_Phone_LockCmd);
    if (v && !g_pulse.phone_lock_frames)  { g_pulse.phone_lock_latched=v; g_pulse.phone_lock_frames=TX2BE_PULSE_FRAMES; }
    v = user_can_rte_read_canSig(RTE_2BE_Key_Cmd);
    if (v && !g_pulse.key_cmd_frames)     { g_pulse.key_cmd_latched=v;  g_pulse.key_cmd_frames=TX2BE_PULSE_FRAMES; }
    v = user_can_rte_read_canSig(RTE_2BE_Key_LockCmd);
    if (v && !g_pulse.key_lock_frames)    { g_pulse.key_lock_latched=v; g_pulse.key_lock_frames=TX2BE_PULSE_FRAMES; }
}
static void pulse_dec(void)
{
    if (g_pulse.phone_cmd_frames)  { if(!--g_pulse.phone_cmd_frames)  g_pulse.phone_cmd_latched=0; }
    if (g_pulse.phone_lock_frames) { if(!--g_pulse.phone_lock_frames) g_pulse.phone_lock_latched=0; }
    if (g_pulse.key_cmd_frames)    { if(!--g_pulse.key_cmd_frames)    g_pulse.key_cmd_latched=0; }
    if (g_pulse.key_lock_frames)   { if(!--g_pulse.key_lock_frames)   g_pulse.key_lock_latched=0; }
}
static uint8_t pulse_get(uint8_t frames, uint8_t latched)
{ return frames ? latched : 0U; }

/*============= RX Handlers (按 DBC 信号布局) =========*/

void CanMatrix_Rx282_Handle(uint8_t state)
{
    switch (state) {
    case RX_STATE_INIT:
        user_can_rte_write_canSig(RTE_282_BCM_LostFlag, 0);
        user_can_rte_write_canSig(RTE_282_BCM_PowerSt, 0);
        user_can_rte_write_canSig(RTE_282_BCM_FLDoorSt, 0);
        user_can_rte_write_canSig(RTE_282_BCM_FRDoorSt, 0);
        user_can_rte_write_canSig(RTE_282_BCM_AntitheftStatus, 0);
        user_can_rte_write_canSig(RTE_282_BCM_DriverDoorLockSt, 0);
        user_can_rte_write_canSig(RTE_282_BCM_BackDoorSt, 0);
        user_can_rte_write_canSig(RTE_VIU_DOOR_RL, 0);
        user_can_rte_write_canSig(RTE_VIU_DOOR_RR, 0);
        break;
    case RX_STATE_LOST:
        user_can_rte_write_canSig(RTE_282_BCM_LostFlag, 1);
        break;
    case RX_STATE_NORMAL:
        /* 参考 Communi CanMatrix 模式: union bitfield 直接读取 */
        user_can_rte_write_canSig(RTE_282_BCM_PowerSt,  g_Rx282Msg.bits.VIU_B_BCMPowerSt);
        user_can_rte_write_canSig(RTE_282_BCM_DriverDoorLockSt, g_Rx282Msg.bits.VIU_BCMDriverDoorLockSt);
        user_can_rte_write_canSig(RTE_282_BCM_FLDoorSt, g_Rx282Msg.bits.VIU_BCMFLDoorSt);
        user_can_rte_write_canSig(RTE_282_BCM_FRDoorSt, g_Rx282Msg.bits.VIU_BCMFRDoorSt);
        user_can_rte_write_canSig(RTE_VIU_DOOR_RL,      g_Rx282Msg.bits.VIU_BCMRLDoorSt);
        user_can_rte_write_canSig(RTE_VIU_DOOR_RR,      g_Rx282Msg.bits.VIU_BCMRRDoorSt);
        user_can_rte_write_canSig(RTE_282_BCM_AntitheftStatus, g_Rx282Msg.bits.BCM_AntitheftStatus);
        user_can_rte_write_canSig(RTE_282_BCM_BackDoorSt, g_Rx282Msg.bits.VIU_BCMBackDoorSt);
        break;
    default: break;
    }
}

void CanMatrix_Rx6DE_Handle(uint8_t state)
{
    switch (state) {
    case RX_STATE_INIT: memset(g_Rx6DEMsg.data, 0, 8); break;
    case RX_STATE_LOST: break;
    case RX_STATE_NORMAL:
        user_can_rte_write_canSig(RTE_6DE_Diag_PhonePair,    g_Rx6DEMsg.bits.Diag_PhonePair);
        user_can_rte_write_canSig(RTE_6DE_Diag_KeyPair,      g_Rx6DEMsg.bits.Diag_KeyPair);
        user_can_rte_write_canSig(RTE_6DE_Diag_KeyDataReset, g_Rx6DEMsg.bits.Diag_KeyDataReset);
        break;
    default: break;
    }
}

/* 0x61A VIN 分帧拼接 (DBC: seq0/seq1/seq2, 每帧 7 字节数据) */
void CanMatrix_Rx61A_Handle(uint8_t state)
{
    switch (state) {
    case RX_STATE_INIT:
        memset(&g_VinReasm, 0, sizeof(g_VinReasm));
        break;
    case RX_STATE_LOST:
        g_VinReasm.vin_valid = false;
        g_VinReasm.complete  = false;
        break;
    case RX_STATE_NORMAL: {
        uint8_t seq = g_Rx61AMsg.bits.VIU_VINInfoNmb;  /* 0/1/2 */
        if (seq > 2U) break;
        uint8_t ofs = seq * 7U;         /* Byte1~7 = 7 字节 */
        uint8_t len = (seq < 2U) ? 7U : 3U;  /* seq2: 仅 3 字节 (17 字节 VIN) */

        memcpy(&g_VinReasm.vin[ofs], &g_Rx61AMsg.data[1], len);
        g_VinReasm.frame_mask |= (1U << seq);

        if ((g_VinReasm.frame_mask & 0x07U) == 0x07U) {
            g_VinReasm.vin_valid = true;
            g_VinReasm.complete  = true;
            vehicle_state_update_vin_from_can(g_VinReasm.vin, 17U, true);
        }
        break;
    }
    default: break;
    }
}

/* 0x217 VIU_B_VIUCrlInfo2 — 续航信息 */
void CanMatrix_Rx217_Handle(uint8_t state)
{
    switch (state) {
    case RX_STATE_INIT:
        memset(g_Rx217Msg.data, 0, 8);
        break;
    case RX_STATE_LOST:
        break;
    case RX_STATE_NORMAL: { 
        uint16_t fuel = (g_Rx217Msg.bits.FuelLeftRange_H << 7) | (g_Rx217Msg.bits.FuelLeftRange_L);
        uint16_t elc  = (g_Rx217Msg.bits.ElcLeftRange_H << 11) | (g_Rx217Msg.bits.ElcLeftRange_M << 3) | (g_Rx217Msg.bits.ElcLeftRange_L);
        uint16_t veh  = (g_Rx217Msg.bits.VehLeftRange_H << 7) | (g_Rx217Msg.bits.VehLeftRange_L);
        uint16_t range = fuel + elc;
 
        user_can_rte_write_canSig(RTE_VIU_REMAINING_RANGE_H, (uint8_t)(range >> 8));
        user_can_rte_write_canSig(RTE_VIU_REMAINING_RANGE_L, (uint8_t)(range & 0xFF));
        break;
    }
    default: break;
    }
}

/*============= TX Handler (0x2BE) ===================*/
uint8_t CanMatrix_Tx2BE_Handle(uint8_t state)
{
    switch (state) {
    case TX_STATE_CHECK:
        pulse_capture();
        return 0;
    case TX_STATE_UPDATE: {
        CanMatrix_Tx2BEMsg_Type *p = &g_Tx2BEMsg;
        p->bits.BLE_PhoneConnect       = user_can_rte_read_canSig(RTE_2BE_Phone_Connect);
        p->bits.BLE_PhoneAutoCmdEnable = user_can_rte_read_canSig(RTE_2BE_Phone_AutoEn);
        p->bits.BLE_KeyConnect         = user_can_rte_read_canSig(RTE_2BE_Key_Connect);
        p->bits.BLE_KeyAutoCmdEnable   = user_can_rte_read_canSig(RTE_2BE_Key_AutoEn);
        p->bits.reserved0_4_7          = 0;
        p->bits.BLE_PhoneCmd       = pulse_get(g_pulse.phone_cmd_frames,  g_pulse.phone_cmd_latched);
        p->bits.BLE_PhoneAutoLockCmd = pulse_get(g_pulse.phone_lock_frames, g_pulse.phone_lock_latched);
        { uint8_t pos = user_can_rte_read_canSig(RTE_2BE_Phone_Pos); p->bits.BLE_PhonePos = (pos < 4U) ? pos : 0U; }
        p->bits.reserved1_7         = 0;
        p->bits.BLE_KeyCmd          = pulse_get(g_pulse.key_cmd_frames,   g_pulse.key_cmd_latched);
        p->bits.BLE_KeyAutoLockCmd  = pulse_get(g_pulse.key_lock_frames,  g_pulse.key_lock_latched);
        { uint8_t pos = user_can_rte_read_canSig(RTE_2BE_Key_Pos);  p->bits.BLE_KeyPos  = (pos < 4U) ? pos : 0U; }
        p->bits.reserved2_7         = 0;
        p->bits.BLE_PhoneRSSI       = user_can_rte_read_canSig(RTE_2BE_Phone_RSSI);
        p->bits.BLE_PhoneDis        = user_can_rte_read_canSig(RTE_2BE_Phone_Dis);
        p->bits.BLE_PhoneStateCode  = 0;
        p->bits.BLE_2BE_Counter     = (g_2BECounter++) & 0x0FU;
        p->bits.BLE_PhoneErrCode    = 0;
        p->data[7] = Crc8(p->data, 7);
        break;
    }
    case TX_STATE_CLEAR:
        pulse_dec();
        break;
    default: break;
    }
    return 0;
}

/*============= 协议层 (BLE→RTE 桥, API 保持不变) =====*/

static void rte_reset_all(void)
{ for (uint8_t i = 0; i < RTE_CAN_SIG_NUM; i++) s_RteSig[i] = 0U; }

void AppProto_Key_SetConnected(bool v)   { user_can_rte_write_canSig(RTE_2BE_Key_Connect, v?1:0); }
void AppProto_Key_SetCmd(uint8_t v)      { user_can_rte_write_canSig(RTE_2BE_Key_Cmd, v); }
void AppProto_Key_SetPos(uint8_t v)      { user_can_rte_write_canSig(RTE_2BE_Key_Pos, v); }
void AppProto_Key_SetLockCmd(uint8_t v)  { user_can_rte_write_canSig(RTE_2BE_Key_LockCmd, v); }
void AppProto_Key_SetAutoEnable(bool v)  { user_can_rte_write_canSig(RTE_2BE_Key_AutoEn, v?1:0); }
void AppProto_Phone_SetConnected(bool v) { user_can_rte_write_canSig(RTE_2BE_Phone_Connect, v?1:0); }
void AppProto_Phone_SetCmd(uint8_t v)    { user_can_rte_write_canSig(RTE_2BE_Phone_Cmd, v); }
void AppProto_Phone_SetPos(uint8_t v)    { user_can_rte_write_canSig(RTE_2BE_Phone_Pos, v); }
void AppProto_Phone_SetLockCmd(uint8_t v){ user_can_rte_write_canSig(RTE_2BE_Phone_LockCmd, v); }
void AppProto_Phone_SetAutoEnable(bool v){ user_can_rte_write_canSig(RTE_2BE_Phone_AutoEn, v?1:0); }
void AppProto_Phone_SetRssi(uint8_t v)   { user_can_rte_write_canSig(RTE_2BE_Phone_RSSI, v); }
void AppProto_Phone_SetDis(uint8_t v)    { user_can_rte_write_canSig(RTE_2BE_Phone_Dis, v); }
uint8_t AppProto_GetVehiclePowerSt(void)    { return user_can_rte_read_canSig(RTE_282_BCM_PowerSt); }
uint8_t AppProto_GetVehicleDoors(void)
{
    uint8_t doors = 0;
    if (user_can_rte_read_canSig(RTE_282_BCM_FLDoorSt)) doors |= 0x01;
    if (user_can_rte_read_canSig(RTE_282_BCM_FRDoorSt)) doors |= 0x02;
    if (user_can_rte_read_canSig(RTE_VIU_DOOR_RL))      doors |= 0x04;
    if (user_can_rte_read_canSig(RTE_VIU_DOOR_RR))      doors |= 0x08;
    return doors;
}
uint8_t AppProto_GetAntitheftStatus(void)  { return user_can_rte_read_canSig(RTE_282_BCM_AntitheftStatus); }

/*============= 消息表 (DBC CAN ID) ==================*/

CanMatrix_RxList_Type  g_CanMatrixRxList[CANMATRIX_RX_LIST_NUM];
CanMatrix_TxList_Type  g_CanMatrixTxList[CANMATRIX_TX_LIST_NUM];

/* Dummy DTC list (CanMatrix.c requires it; unused in BLE project) */
static Bool           s_dummyDtcLost = FALSE;
static void           DummyDtcHandle(void) { }
CanMatrix_DtcList_Type g_CanMatrixDtcList[CANMATRIX_DTC_LIST_NUM];

static void rx_table_init(void)
{
    g_CanMatrixRxList[0] = (CanMatrix_RxList_Type){
        CAN, 0x282U, 500U, 8U, FALSE, CanMatrix_GetRxRaw282(), CanMatrix_Rx282_Handle };
    g_CanMatrixRxList[1] = (CanMatrix_RxList_Type){
        CAN, 0x6DEU, 500U, 8U, FALSE, CanMatrix_GetRxRaw6DE(), CanMatrix_Rx6DE_Handle };
    g_CanMatrixRxList[2] = (CanMatrix_RxList_Type){
        CAN, 0x61AU, 1500U, 8U, FALSE, CanMatrix_GetRxRaw61A(), CanMatrix_Rx61A_Handle };
    g_CanMatrixRxList[3] = (CanMatrix_RxList_Type){
        CAN, 0x217U, 500U, 8U, FALSE, CanMatrix_GetRxRaw217(), CanMatrix_Rx217_Handle };
}

static void tx_table_init(void)
{
    g_CanMatrixTxList[0] = (CanMatrix_TxList_Type){
        CAN, 0x2BEU, 0U, 100U, 8U, CYCLE, CanMatrix_GetTxRaw2BE(), CanMatrix_Tx2BE_Handle };
}

static void dtc_table_init(void)
{
    g_CanMatrixDtcList[0] = (CanMatrix_DtcList_Type){
        0x000U, 9999U, &s_dummyDtcLost, DummyDtcHandle };
}

void CanMatrix_DefaultDataInit(void)
{
    rx_table_init();
    tx_table_init();
    dtc_table_init();
    memset(g_Tx2BEMsg.data, 0, sizeof(g_Tx2BEMsg));
    memset(g_Rx282Msg.data, 0, sizeof(g_Rx282Msg));
    memset(g_Rx6DEMsg.data, 0, sizeof(g_Rx6DEMsg));
    memset(g_Rx61AMsg.data, 0, sizeof(g_Rx61AMsg));
    memset(g_Rx217Msg.data, 0, sizeof(g_Rx217Msg));
    memset(&g_pulse,    0, sizeof(g_pulse));
    memset(&g_VinReasm, 0, sizeof(g_VinReasm));
    rte_reset_all();
}

/*******************************************************
 * Name    :CanTp_Cfg.h
 * Function:BLE CAN Transport Protocol configuration
 *          ISO 15765-2 classic CAN only (no CANFD)
 *******************************************************/
#ifndef _CANTP_CFG_H_
#define _CANTP_CFG_H_

/*-------------include files--------------------------*/
#include "../Types.h"
#include "../Memory.h"

/*-------------external dependencies------------------*/
extern Bool CanIf_ReadUdsRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_WriteUdsTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);

/*-------------UDS addressing-------------------------*/
#define CANTP_FUNC_ADDR_ID      0x7DFU
#define CANTP_PHYS_ADDR_ID      0x7E0U
#define CANTP_RESP_ADDR_ID      0x7E8U

/*-------------buffer sizes---------------------------*/
#define CANTP_RX_BUFF_NB        512U
#define CANTP_TX_BUFF_NB        512U

/*-------------fill value-----------------------------*/
#define CANTP_BYTE_FILL_VALUE   0x00U

/*-------------timer parameters (ms)------------------*/
#define CANTP_N_BS_TIME         150U
#define CANTP_N_CR_TIME         150U
#define CANTP_N_S_TMIN_TIME     20U
#define CANTP_BS_MAX            8U

/*-------------dependency macros----------------------*/
#define CanTp_Copy(p_buff, p_src, len)            Memory_CopyShort(p_buff, p_src, len)
#define CanTp_Fill(p_buff, data, len)             Memory_FillLong(p_buff, data, len)
#define CanTp_ReadRxMsg(p_f, p_i, p_b, p_l)      CanIf_ReadUdsRxMsg(p_f, p_i, p_b, p_l)
#define CanTp_WriteTxMsg(f, i, b, l)              CanIf_WriteUdsTxMsg(f, i, b, l)

/*-------------flow control states-------------------*/
typedef enum {
    CANTP_FS_CTS  = 0x00,
    CANTP_FS_WAIT = 0x01,
    CANTP_FS_OVF  = 0x02,
    CANTP_FS_NULL = 0x03
} CanTp_Fs_Type;

/*-------------main states----------------------------*/
typedef enum {
    CANTP_STATE_INIT   = 0x00,
    CANTP_STATE_NORMAL = 0x01,
    CANTP_STATE_OFF    = 0x02
} CanTp_State_Type;

/*-------------RX states------------------------------*/
typedef enum {
    CANTP_RX_STATE_IDLE = 0x00,
    CANTP_RX_STATE_SF   = 0x01,
    CANTP_RX_STATE_FF   = 0x02,
    CANTP_RX_STATE_CF   = 0x03,
    CANTP_RX_STATE_WAIT = 0x04,
    CANTP_RX_STATE_OK   = 0x05
} CanTp_RxState_Type;

/*-------------TX states------------------------------*/
typedef enum {
    CANTP_TX_STATE_IDLE  = 0x00,
    CANTP_TX_STATE_SF    = 0x01,
    CANTP_TX_STATE_FF    = 0x02,
    CANTP_TX_STATE_CF    = 0x03,
    CANTP_TX_STATE_WAIT  = 0x04,
    CANTP_TX_STATE_START = 0x05
} CanTp_TxState_Type;

/*-------------N_PCI type detection-------------------*/
typedef enum {
    CANTP_RX_SF_NPCI   = 0x00,
    CANTP_RX_FF_NPCI   = 0x01,
    CANTP_RX_CF_NPCI   = 0x02,
    CANTP_RX_FC_NPCI   = 0x03,
    CANTP_RX_NULL_NPCI = 0x04
} CanTp_RxType_Type;

typedef enum {
    CANTP_TX_SF_NPCI   = 0x00,
    CANTP_TX_FF_NPCI   = 0x01,
    CANTP_TX_CF_NPCI   = 0x02,
    CANTP_TX_FC_NPCI   = 0x03,
    CANTP_TX_NULL_NPCI = 0x04
} CanTp_TxType_Type;

/*-------------result codes---------------------------*/
typedef enum {
    CANTP_N_IDLE       = 0x00,
    CANTP_N_OK         = 0x01,
    CANTP_N_TIMEOUT_BS = 0x02,
    CANTP_N_TIMEOUT_CR = 0x03,
    CANTP_N_WRONG_SN   = 0x04,
    CANTP_N_INVALID_FS = 0x05,
    CANTP_N_UNEXP_PDU  = 0x06,
    CANTP_N_BUFF_OVFLW = 0x07,
    CANTP_N_ERROR      = 0x08
} CanTp_Result_Type;

/*-------------timer control--------------------------*/
typedef union {
    uint8_t data;
    struct {
        bits_t N_Bs    : 1;
        bits_t N_Cr    : 1;
        bits_t S_Tmin  : 1;
        bits_t reserved : 5;
    } bits;
} CanTp_TimeCtr_Type;

typedef struct {
    uint16_t N_Bs;
    uint16_t N_Cr;
    uint16_t S_Tmin;
} CanTp_TimeTick_Type;

/*-------------message control------------------------*/
typedef struct {
    CanTp_Fs_Type FS;
    uint8_t       SN;
    uint8_t       BS;
    uint8_t       BSmax;
    uint8_t       STmin;
    uint32_t      point;
} CanTp_MsgCtr_Type;

/*-------------data buffers---------------------------*/
typedef struct {
    Frame_Type frame;
    uint32_t   id;
    uint32_t   len;
    uint8_t    buff[CANTP_RX_BUFF_NB];
} CanTp_RxData_Type;

typedef struct {
    Frame_Type frame;
    uint32_t   id;
    uint32_t   len;
    uint8_t    buff[CANTP_TX_BUFF_NB];
} CanTp_TxData_Type;

#endif /* _CANTP_CFG_H_ */

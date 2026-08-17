#include "spidrv.h"
#include "sl_spidrv_instances.h"
#include "sl_assert.h"


#include "sl_spidrv_eusart_inst_config.h"
SPIDRV_HandleData_t sl_spidrv_eusart_inst_handle_data;
SPIDRV_Handle_t sl_spidrv_eusart_inst_handle = &sl_spidrv_eusart_inst_handle_data;

SPIDRV_Init_t sl_spidrv_eusart_init_inst = {
  .port = SL_SPIDRV_EUSART_INST_PERIPHERAL,
  .portTx = SL_SPIDRV_EUSART_INST_TX_PORT,
  .portRx = SL_SPIDRV_EUSART_INST_RX_PORT,
  .portClk = SL_SPIDRV_EUSART_INST_SCLK_PORT,
#if defined(SL_SPIDRV_EUSART_INST_CS_PORT)
  .portCs = SL_SPIDRV_EUSART_INST_CS_PORT,
#endif
  .pinTx = SL_SPIDRV_EUSART_INST_TX_PIN,
  .pinRx = SL_SPIDRV_EUSART_INST_RX_PIN,
  .pinClk = SL_SPIDRV_EUSART_INST_SCLK_PIN,
#if defined(SL_SPIDRV_EUSART_INST_CS_PIN)
  .pinCs = SL_SPIDRV_EUSART_INST_CS_PIN,
#endif
  .bitRate = SL_SPIDRV_EUSART_INST_BITRATE,
  .frameLength = SL_SPIDRV_EUSART_INST_FRAME_LENGTH,
  .dummyTxValue = 0,
  .type = SL_SPIDRV_EUSART_INST_TYPE,
  .bitOrder = SL_SPIDRV_EUSART_INST_BIT_ORDER,
  .clockMode = SL_SPIDRV_EUSART_INST_CLOCK_MODE,
  .csControl = SL_SPIDRV_EUSART_INST_CS_CONTROL,
  .slaveStartMode = SL_SPIDRV_EUSART_INST_SLAVE_START_MODE,
};

void sl_spidrv_init_instances(void) {
#if !defined(SL_SPIDRV_EUSART_INST_CS_PIN)
  EFM_ASSERT(sl_spidrv_eusart_init_inst.csControl == spidrvCsControlApplication);
#endif 
  SPIDRV_Init(sl_spidrv_eusart_inst_handle, &sl_spidrv_eusart_init_inst);
}

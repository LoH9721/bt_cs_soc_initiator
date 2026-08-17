#include "connect_console.h"
#include "user_log_console.h"
#include "ble_peer_manager_central.h"
#include "ble_peer_manager_filter.h"
#include "sl_status.h"
#include "app.h"
#include "sl_bluetooth.h"
#include "sl_component_catalog.h"
#include "sl_string.h"
#include "sl_iostream.h"
#include "key_gatt_cmd.h"



 

void app_ble_factory_reset_bonds_key(void)
{
  sl_status_t sc;

  if (app_key_conn_handle != SL_BT_INVALID_CONNECTION_HANDLE) {
    (void)ble_peer_manager_central_close_connection(app_key_conn_handle);
    app_key_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
  }

  key_gatt_proto_reset_pairing(); // 清除协议 NVM，确保下次走 Phase A
  sc = sl_bt_sm_delete_bondings();
  USER_LOG_INFO("[UART] factory_reset: bonds cleared sc=0x%04lx" USER_LOG_NL,
                (unsigned long)sc);

  ble_peer_manager_central_init();
  ble_peer_manager_filter_init();
  sc = ble_peer_manager_central_create_connection();
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[UART] factory_reset: scan restart failed 0x%04lx" USER_LOG_NL,
                   (unsigned long)sc);
  }
}




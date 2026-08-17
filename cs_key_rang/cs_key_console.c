

#include "cs_key_console.h"
#include "cs_key_rang.h"
#include "cs_key_rang_config.h"
#include "user_log_console.h"
#include "app.h"
#include "app_config.h"


 

void app_handle_toggle_ranging(void)
{
  sl_status_t sc;

  if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
    USER_LOG_INFO("[APP] no reflector connection" USER_LOG_NL);
    return;
  }
  if (!cs_key_rang_is_connection_ready(app_key_conn_handle)) {
    USER_LOG_INFO("[APP] connection not ready (wait for CS capabilities)" USER_LOG_NL);
    return;
  }

  if (cs_key_rang_is_ranging(app_key_conn_handle)) {
    sc = cs_key_rang_stop(app_key_conn_handle);
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("[APP] ranging stopped (conn %u)" USER_LOG_NL, app_key_conn_handle);
    } else {
      USER_LOG_ERROR("[APP] cs_key_rang_stop failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
    }
  } else {
    sc = cs_key_rang_start(app_key_conn_handle);
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("[APP] ranging started (conn %u, ~%u ms/update)" USER_LOG_NL,
                    app_key_conn_handle,
                    (unsigned)CS_KEY_RANG_TEST_INTERVAL_MS);
    } else if (sc == SL_STATUS_IN_PROGRESS) {
      USER_LOG_INFO("[APP] ranging start pending (wait for CS teardown)" USER_LOG_NL);
    } else {
      USER_LOG_ERROR("[APP] cs_key_rang_start failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
    }
  }
}

void app_handle_print_result(void)
{
  cs_key_rang_result_t result;
  sl_status_t sc;

  if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
    USER_LOG_INFO("[APP] no reflector connection" USER_LOG_NL);
    return;
  }
  if (!cs_key_rang_is_ranging(app_key_conn_handle)) {
    USER_LOG_INFO("[APP] ranging not active (use rang_start)" USER_LOG_NL);
    return;
  }

  sc = cs_key_rang_get_result(app_key_conn_handle, &result);
  if (sc == SL_STATUS_EMPTY) {
    USER_LOG_INFO("[APP] no measurement yet (wait for first CS result)" USER_LOG_NL);
    return;
  }
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[APP] cs_key_rang_get_result failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
    return;
  }

  USER_LOG_INFO("[APP] result [#%lu cnt=%u] dist=%lu mm raw=%lu mm "
                "likeliness=%u.%02u rssi_dist=%lu mm" USER_LOG_NL,
                (unsigned long)result.measurement_count,
                result.ranging_counter,
                (unsigned long)(result.distance_filtered_m * 1000.0f),
                (unsigned long)(result.distance_raw_m * 1000.0f),
                (unsigned)result.likeliness,
                (unsigned)((unsigned long)(result.likeliness * 100.0f) % 100U),
                (unsigned long)(result.distance_rssi_m * 1000.0f));
}
 
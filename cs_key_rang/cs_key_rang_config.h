/***************************************************************************//**
 * @file
 * @brief CS ranging module configuration (edit here for project tuning)
 ******************************************************************************/
#ifndef CS_KEY_RANG_CONFIG_H
#define CS_KEY_RANG_CONFIG_H

#include "cs_initiator_config.h"
#include "sl_bt_api.h"
#include "sl_rtl_clib_api.h"

// -----------------------------------------------------------------------------
// Logging (0 = off, 1 = on)
// -----------------------------------------------------------------------------

/** Master switch: all CS_KEY_RANG_LOG_* macros when 0 */
#ifndef CS_KEY_RANG_LOG_ENABLE
#define CS_KEY_RANG_LOG_ENABLE                    1
#endif

/** Startup configuration banner */
#ifndef CS_KEY_RANG_LOG_CONFIG
#define CS_KEY_RANG_LOG_CONFIG                    1
#endif

/** Per-measurement result lines (distance, RSSI, velocity, ...) */
#ifndef CS_KEY_RANG_LOG_MEASUREMENT
#define CS_KEY_RANG_LOG_MEASUREMENT               0
#endif

/** Stationary-mode estimation progress */
#ifndef CS_KEY_RANG_LOG_PROGRESS
#define CS_KEY_RANG_LOG_PROGRESS                  1
#endif

/** Connection / scan / instance lifecycle */
#ifndef CS_KEY_RANG_LOG_CONNECTION
#define CS_KEY_RANG_LOG_CONNECTION                1
#endif

/** Recoverable warnings (antenna fallback, RTL discard, ...) */
#ifndef CS_KEY_RANG_LOG_WARNING
#define CS_KEY_RANG_LOG_WARNING                   1
#endif

/** Errors (extract failed, max connections, close connection, ...) */
#ifndef CS_KEY_RANG_LOG_ERROR
#define CS_KEY_RANG_LOG_ERROR                     1
#endif

/** Log line prefix */
#ifndef CS_KEY_RANG_LOG_PREFIX
#define CS_KEY_RANG_LOG_PREFIX                    "[CS_KEY_RANG] "
#endif

#ifndef CS_KEY_RANG_LOG_INSTANCE_PREFIX
#define CS_KEY_RANG_LOG_INSTANCE_PREFIX           CS_KEY_RANG_LOG_PREFIX "[%u] "
#endif

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

/**
 * Application-layer minimum interval between measurement reports / logs [ms].
 * Default 500 ms. Does not override BLE CS procedure timing (connection/procedure
 * intervals); those are optimized when remote capabilities are read.
 */
#ifndef CS_KEY_RANG_TEST_INTERVAL_MS
#define CS_KEY_RANG_TEST_INTERVAL_MS              500u
#endif

/** Alias for CS_KEY_RANG_TEST_INTERVAL_MS (log/display throttle in process_action). */
#ifndef CS_KEY_RANG_MEASUREMENT_LOG_INTERVAL_MS
#define CS_KEY_RANG_MEASUREMENT_LOG_INTERVAL_MS   CS_KEY_RANG_TEST_INTERVAL_MS
#endif

/** LCD display refresh period [ms] */
#ifndef CS_KEY_RANG_DISPLAY_REFRESH_RATE_MS
#define CS_KEY_RANG_DISPLAY_REFRESH_RATE_MS       1000u
#endif

/**
 * Quiet time after initiator delete before cs_initiator_create may run again [ms].
 * Prevents SL_STATUS_BT_CTRL_COMMAND_DISALLOWED when restarting CS on the same link.
 */
#ifndef CS_KEY_RANG_TEARDOWN_QUIESCE_MS
#define CS_KEY_RANG_TEARDOWN_QUIESCE_MS             250u
#endif

// -----------------------------------------------------------------------------
// Ranging / CS overrides (defaults from config/cs_initiator_config.h)
// Change here to override without opening Simplicity Studio component UI.
// -----------------------------------------------------------------------------

#ifndef CS_KEY_RANG_CS_MAIN_MODE
#define CS_KEY_RANG_CS_MAIN_MODE                  CS_INITIATOR_DEFAULT_CS_MAIN_MODE
#endif

#ifndef CS_KEY_RANG_CS_SUB_MODE
#define CS_KEY_RANG_CS_SUB_MODE                   CS_INITIATOR_DEFAULT_CS_SUB_MODE
#endif

#ifndef CS_KEY_RANG_ALGO_MODE
#define CS_KEY_RANG_ALGO_MODE                     CS_INITIATOR_DEFAULT_ALGO_MODE
#endif

#ifndef CS_KEY_RANG_CHANNEL_MAP_PRESET
#define CS_KEY_RANG_CHANNEL_MAP_PRESET            CS_INITIATOR_DEFAULT_CHANNEL_MAP_PRESET
#endif

#ifndef CS_KEY_RANG_PROCEDURE_SCHEDULING
#define CS_KEY_RANG_PROCEDURE_SCHEDULING          CS_INITIATOR_DEFAULT_PROCEDURE_SCHEDULING
#endif

#ifndef CS_KEY_RANG_MAX_PROCEDURE_COUNT
#define CS_KEY_RANG_MAX_PROCEDURE_COUNT           CS_INITIATOR_DEFAULT_MAX_PROCEDURE_COUNT
#endif

#endif // CS_KEY_RANG_CONFIG_H

/***************************************************************************//**
 * @file
 * @brief CS ranging module logging macros (controlled by cs_key_rang_config.h)
 ******************************************************************************/
#ifndef CS_KEY_RANG_LOG_H
#define CS_KEY_RANG_LOG_H

#include "cs_key_rang_config.h"
#include "user_log_console.h"

#define CS_KEY_RANG_NL  USER_LOG_NL

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_CONFIG
#define CS_KEY_RANG_LOG_CONFIG_MSG(...)    USER_LOG_INFO(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_CONFIG_MSG(...)    ((void)0)
#endif

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_MEASUREMENT
#define CS_KEY_RANG_LOG_MEASUREMENT_MSG(...)   USER_LOG_INFO(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_MEASUREMENT_MSG(...)   ((void)0)
#endif

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_PROGRESS
#define CS_KEY_RANG_LOG_PROGRESS_MSG(...)  USER_LOG_INFO(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_PROGRESS_MSG(...)  ((void)0)
#endif

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_CONNECTION
#define CS_KEY_RANG_LOG_CONN_MSG(...)      USER_LOG_INFO(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_CONN_MSG(...)      ((void)0)
#endif

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_WARNING
#define CS_KEY_RANG_LOG_WARN_MSG(...)      USER_LOG_INFO(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_WARN_MSG(...)      ((void)0)
#endif

#if CS_KEY_RANG_LOG_ENABLE && CS_KEY_RANG_LOG_ERROR
#define CS_KEY_RANG_LOG_ERROR_MSG(...)     USER_LOG_ERROR(__VA_ARGS__)
#else
#define CS_KEY_RANG_LOG_ERROR_MSG(...)     ((void)0)
#endif

#endif // CS_KEY_RANG_LOG_H

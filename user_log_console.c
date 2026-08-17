/***************************************************************************//**
 * @file
 * @brief 串口日志与交互控制台实现
 ******************************************************************************/
#include "user_log_console.h"

#include "app_config.h"
#include "app_log.h"
#include "sl_iostream.h"
#include "sl_iostream_handles.h"
#include "sl_iostream_init_usart_instances.h"
#include "sl_iostream_uart.h"

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------

#define USER_LOG_CONSOLE_LINE_MAX       160u
#define USER_LOG_CONSOLE_PRINT_BUF_MAX  1024u
#define USER_LOG_CONSOLE_RX_CHUNK_MAX   16u
#define USER_LOG_CONSOLE_PROMPT         "> "

static sl_iostream_t *s_out_stream = NULL;
static sl_iostream_t *s_in_stream = NULL;
static user_log_console_cmd_cb_t s_cmd_cb = NULL;
static void *s_cmd_context = NULL;
static char s_line_buf[USER_LOG_CONSOLE_LINE_MAX];
static char s_print_buf[USER_LOG_CONSOLE_PRINT_BUF_MAX];
static uint16_t s_line_len = 0u;
static bool s_echo_enabled = true;

// -----------------------------------------------------------------------------

static sl_iostream_t *user_log_console_pick_stream(void)
{
  if (sl_iostream_vcom_handle != NULL) {
    return sl_iostream_vcom_handle;
  }
  if (sl_iostream_recommended_console_stream != NULL) {
    return sl_iostream_recommended_console_stream;
  }
  return sl_iostream_get_default();
}

static void user_log_console_enable_rx(void)
{
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
  if (sl_iostream_uart_vcom_handle != NULL) {
    sl_iostream_uart_set_rx_energy_mode_restriction(sl_iostream_uart_vcom_handle, true);
    sl_iostream_uart_wakeup(sl_iostream_uart_vcom_handle);
  }
#endif
}

static void user_log_console_write_str(const char *str)
{
  if (s_out_stream == NULL || str == NULL) {
    return;
  }

  (void)sl_iostream_write(s_out_stream, str, strlen(str));
}

static void user_log_console_echo_char(char ch)
{
  if (!s_echo_enabled || s_out_stream == NULL) {
    return;
  }

  (void)sl_iostream_putchar(s_out_stream, ch);
}

static void user_log_console_print_prompt(void)
{
  user_log_console_write_str(USER_LOG_CONSOLE_PROMPT);
}

static void user_log_console_emit(uint8_t app_level, const char *fmt, va_list ap)
{
  (void)vsnprintf(s_print_buf, sizeof(s_print_buf), fmt, ap);

#if CS_INITIATOR_UART_LOG
  if (s_out_stream != NULL) {
    (void)sl_iostream_write(s_out_stream, s_print_buf, strlen(s_print_buf));
  }
#else
  switch (app_level) {
    case APP_LOG_LEVEL_ERROR:
      app_log_error("%s", s_print_buf);
      break;
    case APP_LOG_LEVEL_WARNING:
      app_log_warning("%s", s_print_buf);
      break;
    case APP_LOG_LEVEL_DEBUG:
      app_log_debug("%s", s_print_buf);
      break;
    default:
      app_log_info("%s", s_print_buf);
      break;
  }
#endif
}

static char user_log_console_tolower(char c)
{
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static bool user_log_console_prefix_i(const char *line, const char *cmd, size_t cmd_len)
{
  for (size_t i = 0u; i < cmd_len; i++) {
    if (user_log_console_tolower(line[i]) != user_log_console_tolower(cmd[i])) {
      return false;
    }
  }
  return true;
}

static void user_log_console_trim_line(char *line)
{
  char *start = line;
  char *end;

  while (*start != '\0' && isspace((unsigned char)*start)) {
    start++;
  }

  if (start != line) {
    memmove(line, start, strlen(start) + 1u);
  }

  end = line + strlen(line);
  while (end > line && isspace((unsigned char)end[-1])) {
    end--;
  }
  *end = '\0';
}

static void user_log_console_dispatch_line(char *line)
{
  user_log_console_trim_line(line);
  if (line[0] == '\0') {
    user_log_console_print_prompt();
    return;
  }

  if (s_cmd_cb != NULL && s_cmd_cb(line, s_cmd_context)) {
    user_log_console_print_prompt();
    return;
  }

  USER_LOG_INFO("[UART] unknown command: %s (try help)" USER_LOG_NL, line);
  user_log_console_print_prompt();
}

static void user_log_console_on_line_complete(void)
{
  s_line_buf[s_line_len] = '\0';
  user_log_console_dispatch_line(s_line_buf);
  s_line_len = 0u;
}

static void user_log_console_feed_char(char ch)
{
  if (ch == '\r' || ch == '\n') {
    user_log_console_write_str("\r\n");
    if (s_line_len > 0u) {
      user_log_console_on_line_complete();
    } else {
      user_log_console_print_prompt();
    }
    return;
  }

  if (ch == '\b' || ch == 0x7F) {
    if (s_line_len > 0u) {
      s_line_len--;
      user_log_console_write_str("\b \b");
    }
    return;
  }

  if (!isprint((unsigned char)ch)) {
    return;
  }

  if (s_line_len >= (USER_LOG_CONSOLE_LINE_MAX - 1u)) {
    USER_LOG_WARN("[UART] line too long, discarded" USER_LOG_NL);
    s_line_len = 0u;
    user_log_console_print_prompt();
    return;
  }

  s_line_buf[s_line_len++] = ch;
  user_log_console_echo_char(ch);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void user_log_console_init(void)
{
  s_out_stream = user_log_console_pick_stream();
  s_in_stream = s_out_stream;
  s_line_len = 0u;
  s_echo_enabled = true;

  user_log_console_enable_rx();

  USER_LOG_INFO("ECU UART console ready. Type 'help' + Enter." USER_LOG_NL);
  user_log_console_print_prompt();
}

void user_log_console_process(void)
{
  char rx_buf[USER_LOG_CONSOLE_RX_CHUNK_MAX];
  size_t nread = 0u;
  sl_status_t sc;

  if (s_in_stream == NULL) {
    s_in_stream = user_log_console_pick_stream();
    s_out_stream = s_in_stream;
    if (s_in_stream == NULL) {
      return;
    }
  }

  user_log_console_enable_rx();

  do {
    nread = 0u;
    sc = sl_iostream_read(s_in_stream, rx_buf, sizeof(rx_buf), &nread);
    if (nread == 0u) {
      break;
    }
    if (sc != SL_STATUS_OK) {
      break;
    }

    for (size_t i = 0u; i < nread; i++) {
      user_log_console_feed_char(rx_buf[i]);
    }
  } while (nread == sizeof(rx_buf));
}

void user_log_console_set_command_handler(user_log_console_cmd_cb_t cb, void *context)
{
  s_cmd_cb = cb;
  s_cmd_context = context;
}

bool user_log_console_parse_0_1(const char *line, const char *cmd, uint8_t *out)
{
  size_t cmd_len;
  const char *p;

  if (line == NULL || cmd == NULL || out == NULL) {
    return false;
  }

  cmd_len = strlen(cmd);
  if (!user_log_console_prefix_i(line, cmd, cmd_len)) {
    return false;
  }

  if (line[cmd_len] != '\0' && line[cmd_len] != ' ') {
    return false;
  }

  p = line + cmd_len;
  while (*p == ' ') {
    p++;
  }

  if (*p == '0' && (p[1] == '\0' || isspace((unsigned char)p[1]))) {
    *out = 0u;
    return true;
  }
  if (*p == '1' && (p[1] == '\0' || isspace((unsigned char)p[1]))) {
    *out = 1u;
    return true;
  }

  return false;
}

void user_log_info(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  user_log_console_emit(APP_LOG_LEVEL_INFO, fmt, ap);
  va_end(ap);
}

void user_log_error(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  user_log_console_emit(APP_LOG_LEVEL_ERROR, fmt, ap);
  va_end(ap);
}

void user_log_warn(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  user_log_console_emit(APP_LOG_LEVEL_WARNING, fmt, ap);
  va_end(ap);
}

void user_log_debug(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  user_log_console_emit(APP_LOG_LEVEL_DEBUG, fmt, ap);
  va_end(ap);
}

/***************************************************************************//**
 * @file
 * @brief 串口日志与交互控制台
 ******************************************************************************/
#ifndef USER_LOG_CONSOLE_H
#define USER_LOG_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/** 日志换行（UART 终端） */
#define USER_LOG_NL  "\r\n"

#define USER_LOG_INFO(...)    user_log_info(__VA_ARGS__)
#define USER_LOG_ERROR(...)   user_log_error(__VA_ARGS__)
#define USER_LOG_WARN(...)    user_log_warn(__VA_ARGS__)
#define USER_LOG_DEBUG(...)   user_log_debug(__VA_ARGS__)

/** 命令行回调：解析成功返回 true，未知命令返回 false */
typedef bool (*user_log_console_cmd_cb_t)(const char *line, void *context);

/** 初始化 VCOM 输出与命令行接收（在 app_log / iostream 就绪后调用） */
void user_log_console_init(void);

/** 轮询串口输入并分发命令（在 app_process_action 中调用） */
void user_log_console_process(void);

/** 注册应用层命令处理器 */
void user_log_console_set_command_handler(user_log_console_cmd_cb_t cb, void *context);

/** 解析 "cmd 0|1" 形式参数，成功时写入 @p out 并返回 true */
bool user_log_console_parse_0_1(const char *line, const char *cmd, uint8_t *out);

void user_log_info(const char *fmt, ...);
void user_log_error(const char *fmt, ...);
void user_log_warn(const char *fmt, ...);
void user_log_debug(const char *fmt, ...);

#endif // USER_LOG_CONSOLE_H

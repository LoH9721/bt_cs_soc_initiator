/***************************************************************************//**
 * @file
 * @brief 串口日志与交互控制台
 ******************************************************************************/
 #ifndef USER_CONSOLE_H
 #define USER_CONSOLE_H
 
 #include <stdbool.h>
 #include <stdint.h>
 


bool app_user_log_on_command(const char *line, void *context);

#endif

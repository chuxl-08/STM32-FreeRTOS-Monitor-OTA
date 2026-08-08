#ifndef __APP_LOG_H
#define __APP_LOG_H

/*
 * 文件职责：
 * 1. 声明 App 层日志初始化和 printf 风格输出接口。
 * 2. 统一经 USART1 输出启动、任务、传感器、显示和上传日志。
 */

void App_Log_Init(void);
void App_LogPrintf(const char *format, ...);

#endif /* __APP_LOG_H */

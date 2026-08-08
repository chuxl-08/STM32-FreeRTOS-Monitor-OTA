#ifndef __APP_PROTOCOL_H
#define __APP_PROTOCOL_H
#include "stm32f10x.h"
#include "app_upload.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明应用层数据打包接口。
 * 2. 将 UploadTask 聚合的数据和 OTA report 组织成 WiFi/TCP 上传用的轻量键值字符串。
 * 3. 统一维护 MODE、TEMP、HUMI、DIST、TMAX、HUMAN、ANGLE、OTA_* 等上传字段。
 *
 * 当前状态：
 * - 已声明系统数据上传字符串打包接口。
 */


AppProtocol_Status_t App_Protocol_BuildUploadString(const AppUpload_Data_t *upload_data,
                                                    char *buffer,
                                                    uint16_t buffer_size);

#endif /* __APP_PROTOCOL_H */

#ifndef __APP_OTA_CONFIG_H
#define __APP_OTA_CONFIG_H

#include "app_upload_config.h"

/*
 * ESP01 App 侧 OTA 配置。
 * WiFi 和 HTTP 服务端地址复用 UploadeTask 配置。
 * UploadTask 和 OtaDownloadTask 共享统一网络。
 */
#define APP_OTA_WIFI_SSID              APP_UPLOAD_WIFI_SSID
#define APP_OTA_WIFI_PASSWORD          APP_UPLOAD_WIFI_PASSWORD
#define APP_OTA_HTTP_HOST              APP_UPLOAD_TCP_HOST
#define APP_OTA_HTTP_PORT              APP_UPLOAD_TCP_PORT

/*
 * 默认路径仅作为无 PATH 字段时的 fallback。
 * 主验证链路优先使用服务器返回的 PATH/VER。
 */
#define APP_OTA_HTTP_PATH_SLOT_A       "/monitor_slot_a_full_v11.pkg"
#define APP_OTA_HTTP_PATH_SLOT_B       "/monitor_slot_b_full_v12.pkg"

#endif /* __APP_OTA_CONFIG_H */

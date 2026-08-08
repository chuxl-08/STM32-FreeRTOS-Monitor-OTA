#ifndef __APP_OTA_H
#define __APP_OTA_H

typedef enum
{
    APP_OTA_OK = 0,
    APP_OTA_ERR_PARAM,
    APP_OTA_ERR_ESP01,
    APP_OTA_ERR_SEND,
    APP_OTA_ERR_TIMEOUT,
    APP_OTA_ERR_OVERFLOW,
    APP_OTA_ERR_HTTP,
    APP_OTA_ERR_LENGTH,
    APP_OTA_ERR_FLASH,
    APP_OTA_ERR_VERIFY,
    APP_OTA_ERR_CONFIG,
    APP_OTA_ERR_VERSION
} AppOtaStatus_t;

AppOtaStatus_t AppOta_RunDownloadVerifyTest(void);
AppOtaStatus_t AppOta_RunUpgradeTriggerTest(void);
const char *AppOta_StatusString(AppOtaStatus_t status);

#endif

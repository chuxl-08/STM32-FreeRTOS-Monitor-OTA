#ifndef __ESP01_H
#define __ESP01_H

#include <stdint.h>

typedef enum
{
    ESP01_OK = 0,
    ESP01_ERR_PARAM,
    ESP01_ERR_TIMEOUT,
    ESP01_ERR_OVERFLOW,
    ESP01_ERR_RESPONSE,
    ESP01_ERR_PORT
} Esp01Status_t;

Esp01Status_t Esp01_Init(void);
Esp01Status_t Esp01_Test(void);
Esp01Status_t Esp01_DisableEcho(void);
Esp01Status_t Esp01_SetWifiModeStation(void);
Esp01Status_t Esp01_SetSingleConnection(void);
Esp01Status_t Esp01_JoinAp(const char *ssid, const char *password);
Esp01Status_t Esp01_StartTcp(const char *host, uint16_t port);
Esp01Status_t Esp01_SendHttpGet(const char *host, uint16_t port, const char *path);
const char *Esp01_StatusString(Esp01Status_t status);

#endif

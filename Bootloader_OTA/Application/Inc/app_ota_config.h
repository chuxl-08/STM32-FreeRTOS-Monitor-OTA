#ifndef __APP_OTA_CONFIG_H
#define __APP_OTA_CONFIG_H

/*
 * Minimal Application OTA verification configuration.
 *
 * Fill these values with the local Wi-Fi and HTTP server parameters before
 * running the ESP01 HTTP download verification app on hardware.
 */
#define APP_OTA_WIFI_SSID          "YOUR_WIFI_SSID"
#define APP_OTA_WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define APP_OTA_HTTP_HOST          "192.168.1.100"
#define APP_OTA_HTTP_PORT          8000U
#define APP_OTA_HTTP_PATH          "/application_slot_b_v2.pkg"

#endif

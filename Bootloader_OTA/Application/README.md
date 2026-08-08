# Application

Bootloader 侧保留的最小验证应用，用于独立验证 Bootloader 到应用的跳转、`TESTING -> CONFIRMED` 确认链路，以及初期 ESP01 HTTP OTA 下载校验流程。

项目主业务应用是仓库根目录下的 `STM32_Monitor/`。本目录适合作为：

- Bootloader 最小跳转确认应用。
- 串口 IAP / A-B 状态机快速验证应用。
- ESP01 HTTP 下载、`+IPD` 解析、HTTP `Content-Length`、固件包 CRC 和版本闸门的参考实现。

## 目录结构与内容

```text
Application/
├── Inc/
│   ├── app_ota.h          HTTP OTA 参考流程接口
│   ├── app_ota_config.h   Wi-Fi、HTTP 服务端和下载路径配置
│   ├── at_parser.h        ESP01 AT 响应解析接口
│   ├── esp01.h            ESP01 AT 命令封装接口
│   └── esp01_port.h       ESP01 USART3 端口适配接口
├── Src/
│   ├── main.c             最小验证应用入口，执行 CONFIRMED 确认和 OTA 触发
│   ├── app_ota.c          HTTP 下载、+IPD 解析、包校验、pending 触发
│   ├── at_parser.c        AT 响应匹配与行解析
│   ├── esp01.c            ESP01 初始化、联网、建链和发送
│   └── esp01_port.c       USART3 发送接收与端口初始化
├── DebugConfig/           Keil 调试配置
├── application.uvprojx    Keil 工程文件
└── README.md
```

## 构建入口

```text
Bootloader_OTA/Application/application.uvprojx
```

Keil target：

| Target | 用途 |
| --- | --- |
| `Application` | 链接到 Slot A，用于最小验证应用 |

## main 流程

`Src/main.c` 启动后会执行以下动作：

1. 设置 `SCB->VTOR = SLOT_A_BASE_ADDR`，使中断向量表指向 Slot A 应用。
2. 初始化 USART1 调试串口。
3. 调用 `UpgradeConfig_SaveConfirmed()`，用于验证 Bootloader `TESTING -> CONFIRMED` 链路。
4. 调用 `AppOta_RunUpgradeTriggerTest()`，保留 HTTP OTA 下载、校验、写 pending 和复位触发参考流程。

默认使用占位 Wi-Fi / HTTP 配置，因此 `AppOta_RunUpgradeTriggerTest()` 会返回 `PARAM` 并跳过真实 ESP01 网络连接。填入真实网络和服务端参数后，该流程会恢复为 HTTP 下载验证路径。

## HTTP OTA 参考流程

`Src/app_ota.c` 保留了 ESP01 HTTP OTA 下载校验参考实现：

- `AppOta_RunDownloadVerifyTest()`：通过 ESP01 发起 HTTP GET，解析 `+IPD`、HTTP header 和 `Content-Length`，接收完整 `.pkg` 到 RAM，写入目标 Slot 并执行固件包校验。
- `AppOta_RunUpgradeTriggerTest()`：复用下载校验流程，校验通过后写入 pending 并触发复位，交给 Bootloader 完成后续 `PENDING_VERIFY -> TESTING`。

公开版本中 Wi-Fi 和 HTTP 服务端参数使用占位值，运行前需要修改：

```c
#define APP_OTA_WIFI_SSID          "YOUR_WIFI_SSID"
#define APP_OTA_WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define APP_OTA_HTTP_HOST          "192.168.1.100"
#define APP_OTA_HTTP_PORT          8000U
#define APP_OTA_HTTP_PATH          "/application_slot_b_v2.pkg"
```

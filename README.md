# STM32-FreeRTOS-Monitor-OTA

基于 STM32F103ZE 的 Bootloader A/B OTA 与 FreeRTOS 多传感器监测应用集成项目。

项目由 Bootloader/OTA 基础设施和 FreeRTOS 业务应用两部分组成：

- `Bootloader_OTA/`：提供 A/B 分区、Bootloader 启动与回滚、固件包校验、升级状态机、串口 IAP 和共享 Flash/Config 模块；其中的 `Application/` 用于最小跳转、确认和 HTTP OTA 下载校验。
- `STM32_Monitor/`：真实 FreeRTOS 业务应用，包含环境采集、自动扫描、OLED 显示、ESP01 上传、应用侧 HTTP OTA 下载和健康确认。

## 项目亮点

- A/B 双槽升级：Slot A / Slot B 分别位于 `0x08008000` 和 `0x08038000`。
- 状态机闭环：`PENDING_VERIFY -> TESTING -> CONFIRMED`。
- 回滚保护：新固件启动后未确认时，Bootloader 回滚到旧 confirmed slot。
- 业务集成：OTA 接入 `STM32_Monitor` 的 FreeRTOS 多任务业务应用，而非独立最小验证应用演示。
- ESP01 HTTP OTA：PC 端服务程序通过 `PATH/VER` 触发应用下载固件包。
- OTA 状态报告：上传报文上报 Config、A/B 版本、最近一次 OTA 结果和失败原因。
- 资源互斥：`UploadTask` 与 `OtaDownloadTask` 通过 ESP01 互斥锁隔离 AT 链路。
- USART3 接收路径优化：使用 DMA 环形缓冲区和 IDLE 中断承接 ESP01 串口字节，经 RingBuffer 交由 AT 解析流程处理；已完成原接线下业务任务共存和压力回归。

## 目录结构

```text
STM32-FreeRTOS-Monitor-OTA/
├── Bootloader_OTA/              Bootloader 与共享 OTA 基础设施
│   ├── Bootloader/              启动选择、A/B 状态机、回滚、跳转和 USART1 IAP
│   ├── Application/             独立验证跳转、确认和 HTTP OTA 下载的最小应用
│   ├── Shared/                  跨工程共享的分区定义、固件校验、Config 和 Flash 接口
│   ├── Libraries/               Bootloader 工程使用的 STM32 SPL、CMSIS 和启动文件
│   ├── Tools/                   固件打包、串口 IAP 和 HTTP 固件服务脚本
│   ├── project.uvmpw            Bootloader 与最小应用的 Keil 工作区
│   └── README.md                Bootloader_OTA 的结构与使用说明
├── STM32_Monitor/               FreeRTOS 多传感器监测业务应用
│   ├── App/                     任务、OTA、上传、检测、显示和协议等应用层逻辑
│   ├── BSP/                     GPIO、USART、I2C、PWM、IWDG 等外设基础封装
│   ├── Drivers/                 传感器、OLED、ESP01、舵机和编码器驱动
│   ├── Middlewares/             FreeRTOS、AT 解析、RingBuffer 和跨任务数据结构
│   ├── System/                  延时等系统辅助模块
│   ├── User/                    应用入口、中断入口和工程配置
│   ├── Library/                 STM32 标准外设库源码
│   ├── Include/                 CMSIS 头文件
│   ├── Start/                   启动文件和系统初始化代码
│   ├── DebugConfig/             Keil 调试配置
│   ├── Tools/                   固件打包、OTA 触发和串口 IAP 辅助脚本
│   ├── docs/public/             公开的架构、构建、硬件和错误处理说明
│   └── README.md                业务应用的功能与构建说明
├── project.uvmpw                总项目 Keil 工作区
└── README.md                    仓库入口说明
```

## 构建入口

使用 Keil MDK：

```text
project.uvmpw
```

或分别打开：

```text
Bootloader_OTA/Bootloader/bootloader.uvprojx
Bootloader_OTA/Application/application.uvprojx
STM32_Monitor/project.uvprojx
```

`STM32_Monitor/project.uvprojx` 包含三个 target：

| Target | 用途 | 应用基址 |
| --- | --- | --- |
| `Target 1` | 原始单 App 调试目标 | `0x08000000` |
| `Monitor Slot A` | Bootloader A/B Slot A App | `0x08008000` |
| `Monitor Slot B` | Bootloader A/B Slot B App | `0x08038000` |

## 打包与 OTA 触发

构建 `Monitor Slot A` / `Monitor Slot B` 后，从总项目根目录执行：

```powershell
python .\Bootloader_OTA\Tools\firmware_pack.py --slot a --version 11 .\STM32_Monitor\Objects\SlotA\monitor_slot_a.bin .\STM32_Monitor\Objects\monitor_slot_a_full_v11.pkg
python .\Bootloader_OTA\Tools\firmware_pack.py --slot b --version 12 .\STM32_Monitor\Objects\SlotB\monitor_slot_b.bin .\STM32_Monitor\Objects\monitor_slot_b_full_v12.pkg
```

应用侧 OTA 触发服务示例：

```powershell
python .\STM32_Monitor\Tools\ota_trigger_server.py --root .\STM32_Monitor\Objects --host 0.0.0.0 --port 8080 --ota-count 1 --ota-path /monitor_slot_b_full_v12.pkg --ota-version 12
```

## 文档

- `Bootloader_OTA/README.md`：Bootloader_OTA 当前目录结构、子目录职责和构建入口说明。
- `Bootloader_OTA/Bootloader/README.md`：Bootloader 启动选择、状态机、跳转和串口 IAP 说明。
- `Bootloader_OTA/Application/README.md`：最小验证应用、`CONFIRMED` 确认和 HTTP 下载校验参考说明。
- `Bootloader_OTA/Tools/README.md`：固件包、串口 IAP 和 HTTP 服务工具说明。
- `STM32_Monitor/README.md`：业务应用功能、任务架构和 Slot A/B 构建说明。
- `STM32_Monitor/docs/public/`：公开项目说明文档。

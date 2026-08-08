# Bootloader_OTA

`Bootloader_OTA/` 是总项目中的 Bootloader 与共享升级基础设施目录，面向 STM32F103ZE 实现 A/B 双槽升级、串口 IAP、固件包校验和升级状态管理。

真实业务应用位于同级目录 `../STM32_Monitor/`。两侧通过本目录下的 `Shared/` 共享 Flash 分区、固件包头、CRC 校验、升级 Config 和 Flash 访问接口。

## 目录结构与内容

```text
Bootloader_OTA/
├── Bootloader/        Bootloader 工程，负责启动选择、状态机、回滚、跳转和 USART1 IAP
├── Application/       最小验证应用，负责 CONFIRMED 确认和 ESP01 HTTP OTA 下载校验参考
├── Shared/            Bootloader、Application、STM32_Monitor 共用的升级协议与 Flash 接口
├── Libraries/         STM32F10x SPL、CMSIS 和启动文件
├── Tools/             固件打包、串口 IAP 下载、HTTP OTA 触发与测试工具
├── project.uvmpw      Keil workspace，聚合 Bootloader 和 Application
└── README.md
```

## 子目录说明

- `Bootloader/`：包含 Bootloader Keil 工程、启动主流程、Slot 选择、状态机推进、回滚保护、App 跳转和 USART1 串口 IAP。该目录的细节见 `Bootloader/README.md`。
- `Application/`：最小验证应用。`main.c` 会先写入 `CONFIRMED`，再进入 HTTP OTA 触发流程；默认公开配置为占位值，会直接跳过真实网络连接。该目录的细节见 `Application/README.md`。
- `Shared/`：放置跨工程共享代码，保证 Bootloader、最小 Application 和 `STM32_Monitor` 对分区、固件包和 Config 的理解一致。
- `Libraries/`：放置 STM32F10x SPL、CMSIS、启动文件和系统初始化相关代码。
- `Tools/`：放置 PC 侧辅助工具，包括固件打包、串口 IAP、HTTP 文件服务和 OTA 触发服务。

## 当前能力

- Bootloader 固定运行在 `0x08000000`。
- Slot A 位于 `0x08008000`，Slot B 位于 `0x08038000`。
- Config 区位于 `0x08068000`。
- 固件包由 `FirmwareHeader_t` 和原始镜像组成。
- 支持 Header CRC、Image CRC、load address、entry address 和 Slot 范围校验。
- 支持 USART1 串口 IAP 写入目标 Slot。
- 支持 `PENDING_VERIFY -> TESTING -> CONFIRMED` 正向确认流程。
- 支持新固件未确认时自动回滚到旧 confirmed Slot。
- 支持最小 Application 确认链路和 ESP01 HTTP OTA 下载校验参考。
- `STM32_Monitor` 作为真实 FreeRTOS 业务应用，已接入 Slot A / Slot B 两个 Keil 构建目标。
- 应用侧支持 ESP01 HTTP OTA、上传与 OTA 的 ESP01 互斥，以及 OTA 状态报告上报。
- USART3 接收路径使用 DMA 环形缓冲区和 IDLE 中断承接 ESP01 串口字节，经 RingBuffer 交由 AT 解析流程处理；已完成原接线下的业务任务共存和压力回归。

## Flash 分区

分区定义位于 `Shared/Inc/memory_map.h`。

```text
0x08000000 - 0x08007FFF  Bootloader  32KB
0x08008000 - 0x08037FFF  Slot A      192KB
0x08038000 - 0x08067FFF  Slot B      192KB
0x08068000 - 0x0806FFFF  Config      32KB
0x08070000 - 0x0807FFFF  Reserved    64KB
```

## 构建入口

使用 Keil MDK 打开：

```text
Bootloader_OTA/project.uvmpw
```

或直接打开子工程：

```text
Bootloader_OTA/Bootloader/bootloader.uvprojx
Bootloader_OTA/Application/application.uvprojx
```

真实 App 使用：

```text
STM32_Monitor/project.uvprojx
```

其中 `Monitor Slot A` / `Monitor Slot B` 构建目标会引用 `../Bootloader_OTA/Shared` 中的共享模块。

## 固件包示例

先在 `STM32_Monitor` 中构建 Slot A/B 构建目标，再打包：

```powershell
python .\Bootloader_OTA\Tools\firmware_pack.py --slot a --version 11 .\STM32_Monitor\Objects\SlotA\monitor_slot_a.bin .\STM32_Monitor\Objects\monitor_slot_a_full_v11.pkg
python .\Bootloader_OTA\Tools\firmware_pack.py --slot b --version 12 .\STM32_Monitor\Objects\SlotB\monitor_slot_b.bin .\STM32_Monitor\Objects\monitor_slot_b_full_v12.pkg
```

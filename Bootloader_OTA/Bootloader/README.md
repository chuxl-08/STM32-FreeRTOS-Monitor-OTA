# Bootloader

本目录是 STM32F103ZE Bootloader 工程，负责上电后的升级状态判断、A/B Slot 选择、串口 IAP、回滚保护和跳转 App。

Bootloader 固定链接在 `0x08000000`，不会承载业务逻辑。真实业务应用使用 `STM32_Monitor/` 的 `Monitor Slot A` / `Monitor Slot B` 构建目标，最小验证应用使用同级 `../Application/`。

## 目录结构与内容

```text
Bootloader/
├── Inc/
│   ├── boot_jump.h      App 跳转接口
│   └── serial_iap.h     USART1 串口 IAP 接口
├── Src/
│   ├── main.c           Bootloader 主流程、状态机、Slot 选择
│   ├── boot_jump.c      App 向量表检查、MSP/Reset_Handler 设置和跳转
│   └── serial_iap.c     串口 IAP 协议、固件包接收和 Flash 写入
├── DebugConfig/         Keil 调试配置
├── bootloader.uvprojx   Keil 工程文件
└── README.md
```

## 主流程

`Src/main.c` 是 Bootloader 入口，核心流程如下：

1. 初始化平台外设与调试串口。
2. 读取 `Shared/` 中定义的升级 Config。
3. 根据 Config 状态决定是否处理 `PENDING_VERIFY`、`TESTING`、`CONFIRMED` 或回滚。
4. 检查候选 Slot 中固件包头、CRC、load address、entry address 和 Slot 范围。
5. 选择可启动 App 后，通过 `BootJump_JumpToApp()` 跳转。
6. 串口 IAP 入口用于通过 USART1 写入指定 Slot，便于无网络条件下验证升级链路。

## 关键文件

- `Src/main.c`：状态机入口，负责选择启动 Slot、处理 pending/testing/rollback，并在需要时进入 IAP。
- `Src/boot_jump.c`：跳转前校验 App 向量表，设置 MSP，跳转到 App Reset_Handler。
- `Src/serial_iap.c`：接收 PC 侧发送的 `.pkg` 固件包，按块写入 Flash，并通过 ACK/NACK 返回结果。
- `../Shared/Inc/memory_map.h`：定义 Bootloader、Slot A、Slot B、Config 和保留区地址。
- `../Shared/Src/firmware_verify.c`：负责固件包头和镜像 CRC 校验。
- `../Shared/Src/upgrade_config_if.c`：负责升级状态和版本信息读写。

## Flash 分区

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
Bootloader_OTA/Bootloader/bootloader.uvprojx
```

也可以从上一级 workspace 打开：

```text
Bootloader_OTA/project.uvmpw
```

## 与 Application / STM32_Monitor 的关系

- `../Application/`：最小验证应用，用于快速验证 Bootloader 跳转、`TESTING -> CONFIRMED` 和 HTTP OTA 下载校验参考。
- `../../STM32_Monitor/`：真实业务应用，包含 FreeRTOS 业务任务、ESP01 上传、应用侧 HTTP OTA 下载、OTA 状态报告和健康确认。

Bootloader 不直接依赖业务任务，只依赖 `Shared/` 中约定的数据结构和 Flash 分区，因此可以同时服务最小验证应用和完整的 `STM32_Monitor` 应用。

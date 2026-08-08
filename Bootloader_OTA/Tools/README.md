# Tools

Bootloader 侧 PC 工具：

- `firmware_pack.py`：给应用 `.bin` 添加 `FirmwareHeader_t` 并计算 CRC32。
- `serial_iap_sender.py`：通过 USART1 串口 IAP 发送固件包到 Bootloader。
- `ota_server.py`：启动本地 HTTP 固件服务器，供 ESP01 HTTP OTA 下载 `.pkg`。

## A/B 固件打包示例

Slot A 和 Slot B 的应用必须分别使用 `STM32_Monitor` 中对应的 Keil 构建目标构建，链接地址分别为：

- Slot A：`0x08008000`
- Slot B：`0x08038000`

从总项目根目录执行：

```powershell
python .\Bootloader_OTA\Tools\firmware_pack.py --slot a --version 11 .\STM32_Monitor\Objects\SlotA\monitor_slot_a.bin .\STM32_Monitor\Objects\monitor_slot_a_full_v11.pkg
python .\Bootloader_OTA\Tools\firmware_pack.py --slot b --version 12 .\STM32_Monitor\Objects\SlotB\monitor_slot_b.bin .\STM32_Monitor\Objects\monitor_slot_b_full_v12.pkg
```

`firmware_pack.py` 会生成由 `FirmwareHeader_t` 和原始镜像组成的 `.pkg`。包头记录目标 Slot 的 `load_address`、`entry_address`、版本和 CRC；真正写入 Slot 起始地址的是原始镜像，因此 Slot 起始处仍然是应用向量表。

## USART1 串口 IAP 示例

串口 IAP 发送的是已经打包好的 `.pkg` 文件：

```powershell
python .\Bootloader_OTA\Tools\serial_iap_sender.py .\STM32_Monitor\Objects\monitor_slot_a_full_v11.pkg --port COM5
```

关联 Bootloader 串口 IAP 第一版协议为：

```text
PC -> Bootloader: 'U' enters IAP window
PC -> Bootloader: "BOTA"
Bootloader -> PC: ACK
PC -> Bootloader: FirmwareHeader_t
Bootloader -> PC: ACK
Bootloader -> PC: READY_IMAGE
PC -> Bootloader: raw image chunk
Bootloader -> PC: ACK per chunk
Bootloader -> PC: ACK after save pending
```

控制字节：

- `ACK = 0x06`
- `NACK = 0x15`
- `READY_IMAGE = 0x11`

## HTTP 固件服务器示例

`ota_server.py` 用于启动本地 HTTP 固件服务器，供 ESP01 OTA 阶段下载 `.pkg`：

```powershell
python .\Bootloader_OTA\Tools\ota_server.py --dir .\STM32_Monitor\Objects --port 8000
```

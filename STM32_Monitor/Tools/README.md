# Tools

PC 端工具：

- `firmware_pack.py`：给应用 `.bin` 添加固件包头并计算 CRC32。
- `serial_iap_sender.py`：通过串口发送固件包到 Bootloader 串口 IAP。
- `ota_server.py`：启动本地 HTTP 固件服务器。
- `ota_trigger_server.py`：接收 `UploadTask` 上传报文，并通过 `PATH/VER` 响应触发应用侧 ESP01 HTTP OTA。

## A/B 固件打包示例

Slot A 和 Slot B 的应用必须分别使用对应 Keil 构建目标构建，链接地址分别为：

- Slot A：`0x08008000`
- Slot B：`0x08038000`

`firmware_pack.py` 会把应用 `.bin` 打成 Bootloader/OTA 使用的 `.pkg`，包结构由 `FirmwareHeader_t` 和原始镜像组成。包头记录目标 Slot 的 `load_address`、`entry_address`、版本和 CRC；真正写入 Slot 起始地址的是原始镜像，因此 Slot 起始处仍然是应用向量表。

```powershell
python .\Tools\firmware_pack.py --slot a --version 11 .\Objects\SlotA\monitor_slot_a.bin .\Objects\monitor_slot_a_full_v11.pkg
python .\Tools\firmware_pack.py --slot b --version 12 .\Objects\SlotB\monitor_slot_b.bin .\Objects\monitor_slot_b_full_v12.pkg
```

如需显式指定地址，也可以使用 `--load-address`，但地址必须匹配 Slot A 或 Slot B：

```powershell
python .\Tools\firmware_pack.py --version 11 --load-address 0x08008000 .\Objects\SlotA\monitor_slot_a.bin .\Objects\monitor_slot_a_full_v11.pkg
```

## USART1 串口 IAP 示例

串口 IAP 发送的是已经打包好的 `.pkg` 文件：

```powershell
python .\Tools\serial_iap_sender.py .\Objects\monitor_slot_a_full_v11.pkg --port COM5
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
python .\Tools\ota_server.py --dir .\Objects --port 8000
```

## 上传/OTA 触发服务示例

`ota_trigger_server.py` 可以模拟 PC 端服务程序：先接收 `UploadTask` 的 TCP 上传报文，再按次数返回 OTA 命令。

```powershell
python .\Tools\ota_trigger_server.py --root .\Objects --host 0.0.0.0 --port 8080 --ota-count 1 --ota-path /monitor_slot_b_full_v12.pkg --ota-version 12
```

- 命令格式为 `OTA=1;PATH=/monitor_slot_b_full_v12.pkg;VER=12`。
- 应用使用 `PATH` 下载包，用 `VER` 对比包头版本；目标槽位仍由应用根据 Bootloader Config 选择非活动槽位。
- HTTP 包放在 `.\Objects` 根目录，例如 `monitor_slot_a_full_v11.pkg` 和 `monitor_slot_b_full_v12.pkg`。
- 工具会打印 `UploadTask` 原始上传报文，并汇总 `OTA_STATE`、`OTA_LAST`、`OTA_DL_STATUS`、`OTA_CONFIRMED` 等字段。

常用验收命令：

```powershell
# 版本不匹配：下载包存在，但服务器期望版本故意写错，应看到 OTA_LAST=FAILED、OTA_DL_STATUS=VERSION
python .\Tools\ota_trigger_server.py --root .\Objects --host 0.0.0.0 --port 8080 --ota-count 1 --ota-path /monitor_slot_a_full_v11.pkg --ota-version 12

# HTTP 失败：下载路径不存在，应看到 OTA_LAST=FAILED、OTA_DL_STATUS=HTTP
python .\Tools\ota_trigger_server.py --root .\Objects --host 0.0.0.0 --port 8080 --ota-count 1 --ota-path /missing.pkg --ota-version 12
```

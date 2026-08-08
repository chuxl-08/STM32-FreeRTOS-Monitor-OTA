#include "stm32f10x.h"                  // Device header
#include "platform.h"
#include "memory_map.h"
#include "boot_jump.h"
#include "firmware_verify.h"
#include "serial_iap.h"
#include "flash_if.h"
#include "upgrade_config_if.h"
#include <stdio.h>

#define IAP_CHUNK_SIZE      128U
#define IAP_TIMEOUT_TICKS   (7200000U * 5U)
/*
 * 当前策略一次测试启动：PENDING_VERIFY 进入 TESTING 时将 boot_attempts 置为 1，
 * 若 App 未确认且再次复位，Bootloader 会进行回滚。后续若多次测试启动，
 * 应在 Boot_HandleTesting 中同步持久化递增 boot_attempts。
 */
#define BOOT_TEST_MAX_ATTEMPTS 1U

/**
 * @brief 启动槽枚举转日志字符串
 * @param slot 启动槽
 * @return
 *      "A"：Slot A
 *      "B"：Slot B
 *      "INVALID"：非法槽
 * @note 只服务串口日志，避免 main 流程里反复写 switch。
 */
static const char *Boot_SlotString(BootSlot_t slot)
{
    switch (slot)
    {
		case BOOT_SLOT_A:
			return "A";
		case BOOT_SLOT_B:
			return "B";
		default:
			return "INVALID";
    }
}

/**
 * @brief 将启动槽转换为 FlashIf 擦写区域
 * @param slot 启动槽
 * @param region 输出 FlashIf 区域
 * @return
 *      0：转换失败
 *      1：转换成功
 * @note Bootloader 的上层逻辑只关心 Slot A/B，真正擦写时才转换成 FlashIf 区域。
 */
static uint8_t Boot_SlotToFlashRegion(BootSlot_t slot, FlashRegion_t *region)
{
    if (region == 0)
    {
        return 0;
    }

    switch (slot)
    {
		case BOOT_SLOT_A:
			*region = FLASH_REGION_SLOT_A;
			return 1;
		case BOOT_SLOT_B:
			*region = FLASH_REGION_SLOT_B;
			return 1;
		default:
			return 0;
    }
}

/**
 * @brief 根据 Config 状态选择本次应该启动的槽
 * @param cfg Config RAM 副本
 * @param cfg_result Config 加载结果
 * @return 本次启动槽
 * @note
 *      - IDLE：默认尝试启动 Slot A，但不把 Slot A 当作已确认版本
 *      - CONFIRMED：启动 confirmed_slot
 *      - PENDING_VERIFY：启动 pending_slot
 *      - TESTING：启动 boot_slot，等待 App 运行后确认
 *      - Config 无效或槽位异常：回退 Slot A
 */
static BootSlot_t Boot_SelectBootSlot(const UpgradeConfig_t *cfg,
                                      UpgradeConfigIfErrorCode_t cfg_result)
{
    BootSlot_t fallback_slot;

    if ((cfg == 0) || (cfg_result != UpgradeConfigIf_OK))
    {
        return BOOT_SLOT_A;
    }

    fallback_slot = MemoryMap_IsSlotValid(cfg->confirmed_slot) ? cfg->confirmed_slot : BOOT_SLOT_A;

    switch (cfg->state)
    {
		case UPGRADE_STATE_IDLE:
			return BOOT_SLOT_A;

		case UPGRADE_STATE_PENDING_VERIFY:
			return MemoryMap_IsSlotValid(cfg->pending_slot) ? cfg->pending_slot : fallback_slot;

		case UPGRADE_STATE_TESTING:
			return MemoryMap_IsSlotValid(cfg->boot_slot) ? cfg->boot_slot : fallback_slot;

		case UPGRADE_STATE_CONFIRMED:
		default:
			return fallback_slot;
    }
}

/**
 * @brief 接收裸 App 镜像并写入目标 Slot
 * @param header 已通过 Slot 头校验的固件包头
 * @param target_slot 本次升级写入槽
 * @return
 *      0：接收、擦写、写入或 CRC 校验失败
 *      1：目标 Slot 镜像写入并校验成功
 * @note 固件包头只保存在 RAM/Config，Slot 起始地址写入的是 App 向量表。
 */
static uint8_t Boot_ReceiveImageToSlot(const FirmwareHeader_t *header,
                                       BootSlot_t target_slot)
{
    FlashRegion_t target_region;
    FlashIfErrorCode_t flashif_result;
    FirmwareVerifyResult_t fw_verify_result;
    uint8_t image_data[IAP_CHUNK_SIZE];
    uint32_t target_base;
    uint32_t remaining;
    uint32_t write_offset = 0U;
    uint32_t chunk;

    if ((header == 0) || !Boot_SlotToFlashRegion(target_slot, &target_region))
    {
        return 0;
    }

    target_base = MemoryMap_SlotBase(target_slot);
    remaining = header->image_size;

    flashif_result = FlashIf_EraseRegion(target_region, 0U, header->image_size);
    if (flashif_result != FLASHIF_OK)
    {
        printf("[IAP] erase slot %s fail %s\r\n",
               Boot_SlotString(target_slot),
               FlashIfErrorCode_String(flashif_result));
        SerialIap_SendNack();
        return 0;
    }

    printf("[IAP] erase slot %s OK, image_size=%lu\r\n",
           Boot_SlotString(target_slot),
           (unsigned long)header->image_size);

    SerialIap_SendReadyImage();

    while (remaining > 0U)
    {
        chunk = (remaining > IAP_CHUNK_SIZE) ? IAP_CHUNK_SIZE : remaining;

        if (SerialIap_ReadExact(image_data, chunk, IAP_TIMEOUT_TICKS) == 0)
        {
            printf("[IAP] image receive timeout\r\n");
            SerialIap_SendNack();
            return 0;
        }

        flashif_result = FlashIf_WriteRegion(target_region, write_offset, image_data, chunk);
        if (flashif_result != FLASHIF_OK)
        {
            printf("[IAP] image write fail %s, offset=%lu chunk=%lu\r\n",
                   FlashIfErrorCode_String(flashif_result),
                   (unsigned long)write_offset,
                   (unsigned long)chunk);
            SerialIap_SendNack();
            return 0;
        }

        printf("[IAP] image write slot=%s addr=0x%08lx size=%lu\r\n",
               Boot_SlotString(target_slot),
               (unsigned long)(target_base + write_offset),
               (unsigned long)chunk);
        SerialIap_SendAck();

        write_offset += chunk;
        remaining -= chunk;
    }

    fw_verify_result = FirmwareVerify_ImageInSlot(header, target_slot);
    if (fw_verify_result != FW_VERIFY_OK)
    {
        printf("[VERIFY] bad slot %s image %s\r\n",
               Boot_SlotString(target_slot),
               FirmwareVerify_ResultString(fw_verify_result));
        SerialIap_SendNack();
        return 0;
    }

    printf("[VERIFY] good slot %s image %s\r\n",
           Boot_SlotString(target_slot),
           FirmwareVerify_ResultString(fw_verify_result));

    return 1;
}

/**
 * @brief 尝试进入 USART1 IAP，并把固件写入指定目标 Slot
 * @param target_slot 本次 IAP 目标槽
 * @return
 *      0：未进入 IAP 或升级失败
 *      1：新镜像已写入目标 Slot 且 Config 已切到 PENDING_VERIFY
 * @note target_slot 由状态机上层决定：IDLE 使用 Slot A，CONFIRMED 使用 confirmed_slot 的另一个槽。
 */
static uint8_t Boot_TrySerialIapToSlot(BootSlot_t target_slot)
{
    FirmwareHeader_t header;
    FirmwareVerifyResult_t fw_verify_result;
    UpgradeConfigIfErrorCode_t cfg_result;

    if (!MemoryMap_IsSlotValid(target_slot))
    {
        printf("[IAP] invalid target slot\r\n");
        return 0;
    }

    printf("[BOOT] OTA target slot=%s\r\n", Boot_SlotString(target_slot));
    printf("[IAP] wait for 'U' to enter\r\n");

    if (SerialIap_WaitEnter(IAP_TIMEOUT_TICKS) == 0)
    {
        printf("[IAP] wait timeout or skip\r\n");
        return 0;
    }

    printf("[IAP] enter\r\n");

    if (SerialIap_WaitSync(IAP_TIMEOUT_TICKS) == 0)
    {
        printf("[IAP] Exit because not receive BOTA\r\n");
        return 0;
    }

    SerialIap_SendAck();
    printf("[IAP] Received BOTA\r\n");

    if (SerialIap_ReadExact((uint8_t *)&header, sizeof(FirmwareHeader_t), IAP_TIMEOUT_TICKS) == 0)
    {
        printf("[IAP] Exit because not receive header\r\n");
        SerialIap_SendNack();
        return 0;
    }

    printf("[IAP] header: version=%lu size=%lu load=0x%08lx entry=0x%08lx crc=0x%08lx\r\n",
           (unsigned long)header.image_version,
           (unsigned long)header.image_size,
           (unsigned long)header.load_address,
           (unsigned long)header.entry_address,
           (unsigned long)header.image_crc32);

    fw_verify_result = FirmwareVerify_HeaderForSlot(&header, target_slot);
    if (fw_verify_result != FW_VERIFY_OK)
    {
        printf("[VERIFY] bad slot %s header %s\r\n",
               Boot_SlotString(target_slot),
               FirmwareVerify_ResultString(fw_verify_result));
        SerialIap_SendNack();
        return 0;
    }

    printf("[VERIFY] good slot %s header %s\r\n",
           Boot_SlotString(target_slot),
           FirmwareVerify_ResultString(fw_verify_result));
    SerialIap_SendAck();

    if (!Boot_ReceiveImageToSlot(&header, target_slot))
    {
        return 0;
    }

    cfg_result = UpgradeConfig_SavePending(&header, target_slot);
    if (cfg_result != UpgradeConfigIf_OK)
    {
        printf("[CFG] save pending slot=%s fail %s\r\n",
               Boot_SlotString(target_slot),
               UpgradeConfigIfErrorCode_String(cfg_result));
        SerialIap_SendNack();
        return 0;
    }

    printf("[CFG] save pending slot=%s OK\r\n", Boot_SlotString(target_slot));
    SerialIap_SendAck();

    return 1;
}

/**
 * @brief 处理 IDLE 状态
 * @param boot_slot 输出本次默认尝试启动的 Slot
 * @return
 *      0：未完成 IAP，后续尝试跳转 Slot A
 *      1：IAP 成功保存 pending，需要系统复位
 * @note IDLE 表示当前没有已确认 App。
 */
static uint8_t Boot_HandleIdle(BootSlot_t *boot_slot)
{
    if (boot_slot == 0)
    {
        return 0;
    }

    *boot_slot = BOOT_SLOT_A;
    printf("[BOOT] IDLE: no confirmed slot, default boot/target slot=A\r\n");

    return Boot_TrySerialIapToSlot(BOOT_SLOT_A);
}

/**
 * @brief 处理 CONFIRMED 状态
 * @param cfg Config RAM 副本
 * @param boot_slot 输出本次应启动的已确认 Slot
 * @return
 *      0：未完成 IAP，后续跳转 confirmed_slot
 *      1：IAP 成功保存 pending，需要系统复位
 * @note CONFIRMED 表示已有稳定 App，因此 OTA 目标应选择另一个 Slot。
 */
static uint8_t Boot_HandleConfirmed(const UpgradeConfig_t *cfg,
                                    BootSlot_t *boot_slot)
{
    BootSlot_t confirmed_slot;
    BootSlot_t target_slot;

    if ((cfg == 0) || (boot_slot == 0))
    {
        return 0;
    }

    confirmed_slot = cfg->confirmed_slot;

    if (!MemoryMap_IsSlotValid(confirmed_slot))
    {
        *boot_slot = BOOT_SLOT_A;
        printf("[BOOT] CONFIRMED state has invalid confirmed slot, fallback to IDLE policy\r\n");
        return Boot_TrySerialIapToSlot(BOOT_SLOT_A);
    }

    *boot_slot = confirmed_slot;
    target_slot = MemoryMap_OtherSlot(confirmed_slot);

    printf("[BOOT] confirmed slot=%s, OTA target slot=%s\r\n",
           Boot_SlotString(confirmed_slot),
           Boot_SlotString(target_slot));

    return Boot_TrySerialIapToSlot(target_slot);
}

/**
 * @brief 处理 PENDING_VERIFY 状态
 * @param cfg Config RAM 副本
 * @param boot_slot 输出本次应跳转的 pending 槽
 * @return
 *      0：pending 槽不安全，禁止跳转
 *      1：pending 镜像校验成功，已切换为 TESTING，可跳转测试 App
 * @note 只负责 Bootloader 侧“允许试启动”；真正 CONFIRMED 后续由 App 健康检查触发。
 */
static uint8_t Boot_HandlePendingVerify(const UpgradeConfig_t *cfg,
                                        BootSlot_t *boot_slot)
{
    FirmwareVerifyResult_t fw_verify_result;
    UpgradeConfigIfErrorCode_t cfg_result;
    BootSlot_t pending_slot;

    if ((cfg == 0) || (boot_slot == 0))
    {
        return 0;
    }

    pending_slot = cfg->pending_slot;
    printf("[CFG] pending found slot=%s state=%s\r\n",
           Boot_SlotString(pending_slot),
           UpgradState_String(cfg->state));

    if (!MemoryMap_IsSlotValid(pending_slot))
    {
        printf("[CFG] pending slot invalid\r\n");
        return 0;
    }

    fw_verify_result = FirmwareVerify_HeaderForSlot(&cfg->pending_header, pending_slot);
    if (fw_verify_result != FW_VERIFY_OK)
    {
        printf("[VERIFY] bad pending header %s\r\n", FirmwareVerify_ResultString(fw_verify_result));
        return 0;
    }

    fw_verify_result = FirmwareVerify_ImageInSlot(&cfg->pending_header, pending_slot);
    if (fw_verify_result != FW_VERIFY_OK)
    {
        printf("[VERIFY] bad pending image %s\r\n", FirmwareVerify_ResultString(fw_verify_result));
        return 0;
    }

    cfg_result = UpgradeConfig_SaveState(UPGRADE_STATE_TESTING);
    if (cfg_result != UpgradeConfigIf_OK)
    {
        printf("[CFG] save TESTING state fail %s\r\n", UpgradeConfigIfErrorCode_String(cfg_result));
        return 0;
    }

    *boot_slot = pending_slot;
    printf("[CFG] pending verify OK, enter TESTING slot=%s\r\n", Boot_SlotString(pending_slot));

    return 1;
}

/**
 * @brief 处理 TESTING 状态
 * @param cfg Config RAM 副本
 * @param boot_slot 输出本次应跳转的槽
 * @return
 *      0：boot 槽不安全，禁止跳转
 *      1：TESTING 槽可继续试启动，或已回滚到 confirmed slot 并允许跳转
 * @note 再次复位，TESTING 状态下 App 未写入 CONFIRMED 且超过阈值时，回滚到旧 confirmed slot。
 */
static uint8_t Boot_HandleTesting(const UpgradeConfig_t *cfg, BootSlot_t *boot_slot)
{
    UpgradeConfigIfErrorCode_t cfg_result;

    if ((cfg == 0) || (boot_slot == 0))
    {
        return 0;
    }

    if(cfg->state == UPGRADE_STATE_TESTING)
    {
        if(cfg->boot_attempts >= BOOT_TEST_MAX_ATTEMPTS)
        {
            printf("[BOOT] attempt to confirm slot %s exceed threshold, try to rollback to slot %s\r\n", Boot_SlotString(cfg->pending_slot), Boot_SlotString(cfg->rollback_slot) );
            BootSlot_t rollback_slot = cfg->rollback_slot;
            if(MemoryMap_IsSlotValid(rollback_slot) == 0)
            {
                return 0;
            }
            cfg_result = UpgradeConfig_RollbackToConfirmed();
            if(cfg_result == UpgradeConfigIf_OK)
            {
                *boot_slot = rollback_slot;
                printf("[CFG] update testing state, rollback to confirmed\r\n");
            }
            else
            {
                printf("[CFG] update testing state fail %s\r\n", UpgradeConfigIfErrorCode_String(cfg_result));
                return 0;
            }
        }
        else
        {
            *boot_slot = cfg->boot_slot;
        }

        return 1;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief 校验并跳转到指定 Slot，失败则停留在 Bootloader
 * @param boot_slot 本次准备启动的 Slot
 * @return
 *      None
 * @note BootJump_IsAppValid 只做向量表最小检查，完整镜像 CRC 在升级状态机中完成。
 */
static void Boot_JumpSlotOrStay(BootSlot_t boot_slot)
{
    uint32_t target_base;

    if (!MemoryMap_IsSlotValid(boot_slot))
    {
        printf("[BOOT] invalid boot slot, stay in bootloader\r\n");
        return;
    }

    target_base = MemoryMap_SlotBase(boot_slot);
    BootJump_PrintAppInfo(target_base);

    if (BootJump_IsAppValid(target_base))
    {
        printf("[BOOT] slot %s app valid\r\n", Boot_SlotString(boot_slot));
        printf("[BOOT] app @ 0x%08lx\r\n", (unsigned long)target_base);
        printf("[BOOT] jump to app\r\n");
        Platform_Delay(720000U);

        BootJump_JumpToApp(target_base);
    }

    printf("[BOOT] slot %s app invalid, stay in bootloader\r\n", Boot_SlotString(boot_slot));
}

int main(void)
{
    UpgradeConfig_t upgrad_config;
    UpgradeConfigIfErrorCode_t cfg_result;
    BootSlot_t boot_slot;
    uint8_t allow_jump = 1U;

    Platform_InitUsart1();
    Platform_PutString("[BOOT] STM32F103 Bootloader\r\n");
    printf("[BOOT] Slot A @ 0x%08lx, Slot B @ 0x%08lx\r\n",
           (unsigned long)SLOT_A_BASE_ADDR,
           (unsigned long)SLOT_B_BASE_ADDR);

    printf("[CFG] load upgrade config @ 0x%08lx\r\n", (unsigned long)UPGRADE_CONFIG_BASE_ADDR);
    cfg_result = UpgradeConfig_Load(&upgrad_config);
    boot_slot = Boot_SelectBootSlot(&upgrad_config, cfg_result);
    if (cfg_result == UpgradeConfigIf_OK)
    {
        printf("[CFG] state=%s confirmed=%s pending=%s boot=%s\r\n",
               UpgradState_String(upgrad_config.state),
               Boot_SlotString(upgrad_config.confirmed_slot),
               Boot_SlotString(upgrad_config.pending_slot),
               Boot_SlotString(upgrad_config.boot_slot));
    }
    else
    {
        printf("[CFG] invalid because of %s, use default boot slot %s\r\n", UpgradeConfigIfErrorCode_String(cfg_result), Boot_SlotString(boot_slot));
    }

    switch (upgrad_config.state)
    {
		case UPGRADE_STATE_IDLE:
			if (Boot_HandleIdle(&boot_slot))
			{
				printf("[BOOT] pending saved, reset to verify new slot\r\n");
				Platform_Delay(720000U);
				NVIC_SystemReset();
			}
			break;

		case UPGRADE_STATE_CONFIRMED:
			if (Boot_HandleConfirmed(&upgrad_config, &boot_slot))
			{
				printf("[BOOT] pending saved, reset to verify new slot\r\n");
				Platform_Delay(720000U);
				NVIC_SystemReset();
			}
			break;

        case UPGRADE_STATE_PENDING_VERIFY:
            allow_jump = Boot_HandlePendingVerify(&upgrad_config, &boot_slot);
            break;

        case UPGRADE_STATE_TESTING:
            allow_jump = Boot_HandleTesting(&upgrad_config, &boot_slot);
            break;

        default:
            printf("[BOOT] state=%s not handled, fallback slot=%s\r\n",
                UpgradState_String(upgrad_config.state),
                Boot_SlotString(boot_slot));
            break;
    }

    if (allow_jump)
    {
        Boot_JumpSlotOrStay(boot_slot);
    }
    else
    {
        printf("[BOOT] stop before jump because current upgrade state is not safe\r\n");
    }

    while (1)
    {
        Platform_Delay(720000U);
    }
}

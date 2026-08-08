#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

/*
 * @brief Bootloader A/B Slot 枚举。
 * @note
 *      confirmed_slot、pending_slot、boot_slot 和 rollback_slot 都使用该枚举表达。
 */
typedef enum
{
    BOOT_SLOT_INVALID = 0,
    BOOT_SLOT_A,
    BOOT_SLOT_B
} BootSlot_t;

#define FLASH_BASE_ADDR             0x08000000UL
#define FLASH_TOTAL_SIZE            (512UL * 1024UL)
#define FLASH_END_ADDR              (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE)

#define SRAM_BASE_ADDR              0x20000000UL
#define SRAM_TOTAL_SIZE             (64UL * 1024UL)
#define SRAM_END_ADDR               (SRAM_BASE_ADDR + SRAM_TOTAL_SIZE)

#define BOOTLOADER_BASE_ADDR        0x08000000UL
#define BOOTLOADER_SIZE             (32UL * 1024UL)

#define SLOT_A_BASE_ADDR			0x08008000UL
#define SLOT_A_SIZE					(192UL * 1024UL)

#define SLOT_B_BASE_ADDR			0x08038000UL
#define SLOT_B_SIZE					(192UL * 1024UL)

#define UPGRADE_CONFIG_BASE_ADDR    0x08068000UL
#define UPGRADE_CONFIG_SIZE         (32UL * 1024UL)

#define RESERVED_BASE_ADDR          0x08070000UL
#define RESERVED_SIZE               (64UL * 1024UL)

#define FLASH_PAGE_SIZE             2048UL


/**
 * @brief 落入 Flash 区域判断。
 * @param {uint32_t} addr 起始地址。
 * @param {uint32_t} size 大小。
 * @return 1：落入。0：未落入。
 * @note 用于检查固件入口地址、镜像范围等是否位于整片 Flash 内。
 */
static inline uint32_t MemoryMap_IsFlashRange(uint32_t addr, uint32_t size)
{
    return (addr >= FLASH_BASE_ADDR) &&
           (size <= FLASH_TOTAL_SIZE) &&
           (addr <= (FLASH_END_ADDR - size));
}

/**
 * @brief 落入 SRAM 区域判断。
 * @param {uint32_t} addr 起始地址。
 * @param {uint32_t} size 大小。
 * @return 1：落入。0：未落入。
 * @note 用于检查 App 初始 MSP 是否位于 STM32F103ZE SRAM 范围。
 */
static inline uint32_t MemoryMap_IsSRAMRange(uint32_t addr, uint32_t size)
{
    return (addr >= SRAM_BASE_ADDR) &&
           (size <= SRAM_TOTAL_SIZE) &&
           (addr <= (SRAM_END_ADDR - size));
}

/**
 * @brief 落入 Config 区域判断。
 * @param {uint32_t} addr 起始地址。
 * @param {uint32_t} size 大小。
 * @return 1：落入。0：未落入。
 * @note Config 区用于保存 A/B 升级状态和 CRC。
 */
static inline uint32_t MemoryMap_IsConfigRange(uint32_t addr, uint32_t size)
{
    return (addr >= UPGRADE_CONFIG_BASE_ADDR) &&
           (size <= UPGRADE_CONFIG_SIZE) &&
           (addr <= (UPGRADE_CONFIG_BASE_ADDR + UPGRADE_CONFIG_SIZE - size));
}

/**
 * @brief 获取 Slot 起始地址。
 * @param {BootSlot_t} slot 目标 Slot。
 * @return Slot A 或 Slot B 起始地址；非法 Slot 返回 0。
 * @note Bootloader 跳转、固件校验和 Flash 写入都会通过该函数取得目标 Slot 地址。
 */
static inline uint32_t MemoryMap_SlotBase(BootSlot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return SLOT_A_BASE_ADDR;   // 0x08008000
    }
    else if (slot == BOOT_SLOT_B)
    {
        return SLOT_B_BASE_ADDR;   // 0x08038000
    }

    return 0U;
}

/**
 * @brief 获取 Slot 容量。
 * @param {BootSlot_t} slot 目标 Slot。
 * @return Slot A 或 Slot B 容量；非法 Slot 返回 0。
 * @note 当前 Slot A/B 容量相同，均为 192KB。
 */
static inline uint32_t MemoryMap_SlotSize(BootSlot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return SLOT_A_SIZE;
    }
    else if (slot == BOOT_SLOT_B)
    {
        return SLOT_B_SIZE;
    }

    return 0U;
}

/**
 * @brief Slot 有效性判断。
 * @param {BootSlot_t} slot 待判断 Slot。
 * @return 1：Slot A 或 Slot B。0：非法 Slot。
 * @note Config 中 confirmed、pending、boot、rollback 字段都需要先做该检查。
 */
static inline uint32_t MemoryMap_IsSlotValid(BootSlot_t slot)
{
    if((slot == BOOT_SLOT_A) || (slot == BOOT_SLOT_B))
    {
        return 1U;
    }
    else return 0U;
}

/**
 * @brief 判断地址范围是否完全落入指定 Slot。
 * @param {BootSlot_t} slot 目标 Slot。
 * @param {uint32_t} addr 起始地址。
 * @param {uint32_t} size 大小。
 * @return 1：完全落入。0：未落入或 Slot 非法。
 * @note 用于校验 load_address、entry_address 和镜像大小，避免跨 Slot 或越界。
 */
static inline uint32_t MemoryMap_IsSlotRange(BootSlot_t slot, uint32_t addr, uint32_t size)
{
    if(slot == BOOT_SLOT_A)
    {
        return (addr >= SLOT_A_BASE_ADDR) &&
           (size <= SLOT_A_SIZE) &&
           (addr <= (SLOT_A_BASE_ADDR + SLOT_A_SIZE - size));
    }
    else if(slot == BOOT_SLOT_B)
    {
        return (addr >= SLOT_B_BASE_ADDR) &&
           (size <= SLOT_B_SIZE) &&
           (addr <= (SLOT_B_BASE_ADDR + SLOT_B_SIZE - size));
    }
    else
        return 0U;
}

/**
 * @brief 根据固件 load_address 判断目标 Slot。
 * @param {uint32_t} addr 固件包头中的 load_address。
 * @return BOOT_SLOT_A、BOOT_SLOT_B 或 BOOT_SLOT_INVALID。
 * @note PC 打包工具和 Bootloader 校验都要求 load_address 精确等于 Slot 起始地址。
 */
static inline BootSlot_t MemoryMap_SlotFromLoadAddress(uint32_t addr)
{
    if(addr == SLOT_A_BASE_ADDR)
    {
        return BOOT_SLOT_A;
    }
    else if(addr == SLOT_B_BASE_ADDR)
    {
        return BOOT_SLOT_B;
    }
    else
        return BOOT_SLOT_INVALID;
}

/**
 * @brief 获取另一个 Slot。
 * @param {BootSlot_t} slot 当前 Slot。
 * @return 当前为 Slot A 时返回 Slot B；当前为 Slot B 时返回 Slot A；非法 Slot 返回 BOOT_SLOT_INVALID。
 * @note confirmed slot 的另一个 Slot 就是 OTA/IAP 写入目标。
 */
static inline BootSlot_t MemoryMap_OtherSlot(BootSlot_t slot)
{
    if(MemoryMap_IsSlotValid(slot))
    {
        return (slot == BOOT_SLOT_A) ? BOOT_SLOT_B : BOOT_SLOT_A;
    }
    else
        return BOOT_SLOT_INVALID;
}

#endif

#include "crc32.h"

uint32_t Crc32_Update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;

    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

uint32_t Crc32_Calculate(const void *data, size_t length)
{
    return Crc32_Update(0x00000000UL, data, length);
}


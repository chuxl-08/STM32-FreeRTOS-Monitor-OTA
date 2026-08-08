#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t Crc32_Calculate(const void *data, size_t length);
uint32_t Crc32_Update(uint32_t crc, const void *data, size_t length);

#endif


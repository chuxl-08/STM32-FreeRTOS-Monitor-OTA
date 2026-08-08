#!/usr/bin/env python3
"""Pack an application .bin with the project firmware header."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


FIRMWARE_MAGIC = 0x424F5441
HEADER_VERSION = 1
IMAGE_TYPE_APP = 1
SLOT_A_BASE = 0x08008000
SLOT_B_BASE = 0x08038000
SLOT_SIZE = 192 * 1024
SRAM_BASE = 0x20000000
SRAM_SIZE = 64 * 1024
SRAM_END = SRAM_BASE + SRAM_SIZE
HEADER_STRUCT = struct.Struct("<10I")

SLOTS = {
    "a": SLOT_A_BASE,
    "b": SLOT_B_BASE,
}


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_int(value: str) -> int:
    return int(value, 0)


def resolve_load_address(slot: str | None, load_address: int | None) -> tuple[str, int]:
    if slot is None and load_address is None:
        return "a", SLOT_A_BASE

    if slot is not None:
        slot_base = SLOTS[slot]
        if load_address is not None and load_address != slot_base:
            raise ValueError(
                f"--slot {slot.upper()} expects load address 0x{slot_base:08X}, "
                f"got 0x{load_address:08X}"
            )
        return slot, slot_base

    for name, base in SLOTS.items():
        if load_address == base:
            return name, base

    raise ValueError(
        "--load-address must match Slot A 0x08008000 or Slot B 0x08038000"
    )


def validate_image_for_slot(image: bytes, slot: str, load_address: int) -> tuple[int, int]:
    if len(image) < 8:
        raise ValueError("image is too small to contain an STM32 vector table")

    if len(image) > SLOT_SIZE:
        raise ValueError(
            f"image size {len(image)} exceeds Slot {slot.upper()} size {SLOT_SIZE}"
        )

    initial_msp, entry_address = struct.unpack_from("<2I", image, 0)
    entry_without_thumb = entry_address & ~1
    slot_end = load_address + SLOT_SIZE

    if not (SRAM_BASE <= initial_msp <= SRAM_END):
        raise ValueError(
            f"initial MSP 0x{initial_msp:08X} is outside SRAM "
            f"0x{SRAM_BASE:08X}-0x{SRAM_END:08X}"
        )

    if (entry_address & 1) == 0:
        raise ValueError(
            f"entry address 0x{entry_address:08X} is not a Thumb entry"
        )

    if not (load_address <= entry_without_thumb < slot_end):
        raise ValueError(
            f"entry address 0x{entry_address:08X} is outside Slot {slot.upper()} "
            f"0x{load_address:08X}-0x{slot_end - 1:08X}"
        )

    return initial_msp, entry_address


def build_header(image: bytes, version: int, load_address: int, flags: int) -> bytes:
    entry_address = struct.unpack_from("<I", image, 4)[0]
    fields = [
        FIRMWARE_MAGIC,
        HEADER_VERSION,
        IMAGE_TYPE_APP,
        version,
        len(image),
        crc32(image),
        load_address,
        entry_address,
        flags,
        0,
    ]
    header_without_crc = HEADER_STRUCT.pack(*fields)
    fields[-1] = crc32(header_without_crc)
    return HEADER_STRUCT.pack(*fields)


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a Bootloader/OTA firmware package.")
    parser.add_argument("input_bin", type=Path, help="application binary")
    parser.add_argument("output_pkg", type=Path, help="output package, header + image")
    parser.add_argument("--version", type=int, required=True, help="integer firmware version")
    parser.add_argument("--slot", choices=("a", "b"), help="target slot, default: a")
    parser.add_argument("--load-address", type=parse_int, help="target slot base address")
    parser.add_argument("--flags", type=parse_int, default=0)
    args = parser.parse_args()

    image = args.input_bin.read_bytes()
    slot, load_address = resolve_load_address(args.slot, args.load_address)
    initial_msp, entry_address = validate_image_for_slot(image, slot, load_address)
    header = build_header(image, args.version, load_address, args.flags)
    args.output_pkg.write_bytes(header + image)

    print(f"input={args.input_bin}")
    print(f"output={args.output_pkg}")
    print(f"slot={slot.upper()}")
    print(f"version={args.version}")
    print(f"size={len(image)}")
    print(f"load_address=0x{load_address:08X}")
    print(f"initial_msp=0x{initial_msp:08X}")
    print(f"entry_address=0x{entry_address:08X}")
    print(f"image_crc32=0x{crc32(image):08X}")
    print(f"header_crc32=0x{struct.unpack_from('<I', header, 36)[0]:08X}")


if __name__ == "__main__":
    main()

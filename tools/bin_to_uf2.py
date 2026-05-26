#!/usr/bin/env python3
"""Convert a flat RP2xxx flash binary to UF2 without invoking picotool.

This is intentionally narrow: it emits 256-byte UF2 payload blocks for a binary
that should be loaded at a caller-provided flash address. For RP2350 images it
can also prepend the absolute ignore block used by picotool's --abs-block path.
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_FLAG_EXTENSION_FLAGS_PRESENT = 0x00008000
UF2_EXTENSION_RP2_IGNORE_BLOCK = 0x9957E304

ABSOLUTE_FAMILY_ID = 0xE48BFF57
RP2350_ARM_S_FAMILY_ID = 0xE48BFF59
UF2_PAGE_SIZE = 256
UF2_DATA_SIZE = 476


FAMILIES = {
    "rp2350-arm-s": RP2350_ARM_S_FAMILY_ID,
}


def parse_u32(text: str) -> int:
    return int(text, 0)


def make_block(
    *,
    flags: int,
    target_addr: int,
    payload: bytes,
    block_no: int,
    num_blocks: int,
    family_id: int,
    extension_word: int | None = None,
) -> bytes:
    if len(payload) > UF2_PAGE_SIZE:
        raise ValueError("payload cannot exceed 256 bytes")

    data = bytearray(UF2_DATA_SIZE)
    data[: len(payload)] = payload
    if extension_word is not None:
        struct.pack_into("<I", data, UF2_PAGE_SIZE, extension_word)

    return struct.pack(
        "<IIIIIIII476sI",
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        flags,
        target_addr,
        UF2_PAGE_SIZE,
        block_no,
        num_blocks,
        family_id,
        bytes(data),
        UF2_MAGIC_END,
    )


def convert(
    input_path: Path,
    output_path: Path,
    base_addr: int,
    family_id: int,
    abs_block_addr: int | None,
) -> None:
    image = input_path.read_bytes()
    if not image:
        raise SystemExit(f"{input_path} is empty")

    block_count = math.ceil(len(image) / UF2_PAGE_SIZE)
    with output_path.open("wb") as out:
        if abs_block_addr is not None:
            out.write(
                make_block(
                    flags=UF2_FLAG_FAMILY_ID_PRESENT | UF2_FLAG_EXTENSION_FLAGS_PRESENT,
                    target_addr=abs_block_addr,
                    payload=bytes([0xEF]) * UF2_PAGE_SIZE,
                    block_no=0,
                    num_blocks=2,
                    family_id=ABSOLUTE_FAMILY_ID,
                    extension_word=UF2_EXTENSION_RP2_IGNORE_BLOCK,
                )
            )

        for block_no in range(block_count):
            offset = block_no * UF2_PAGE_SIZE
            payload = image[offset : offset + UF2_PAGE_SIZE]
            out.write(
                make_block(
                    flags=UF2_FLAG_FAMILY_ID_PRESENT,
                    target_addr=base_addr + offset,
                    payload=payload,
                    block_no=block_no,
                    num_blocks=block_count,
                    family_id=family_id,
                )
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--base", type=parse_u32, default=0x10000000)
    parser.add_argument("--family", choices=sorted(FAMILIES), default="rp2350-arm-s")
    parser.add_argument("--abs-block", type=parse_u32, default=0x10FFFF00)
    parser.add_argument("--no-abs-block", action="store_true")
    args = parser.parse_args()

    convert(
        args.input,
        args.output,
        args.base,
        FAMILIES[args.family],
        None if args.no_abs_block else args.abs_block,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

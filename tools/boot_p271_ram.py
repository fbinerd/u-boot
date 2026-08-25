#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Load a P271 GXL boot image into volatile RAM over the Amlogic USB ROM."""

import argparse
import hashlib
import logging
from pathlib import Path
import time

from pyamlboot.pyamlboot import AmlogicSoC


DDR_LOAD = 0xD9000000
BL2_PARAMS = 0xD900C000
UBOOT_LOAD = 0x0200C000
TPL_CHUNK_SIZE = 64 * 1024
DDR_TEST_PATTERN = bytes(range(64))
FACTORY_BL2_SHA256 = (
    "a1b68fca03f9632a880e837933c0caabf32272170131bc7f35d3f45f9e37ebbb"
)


def read_checked(path: Path, expected_size: int | None = None) -> bytes:
    data = path.read_bytes()
    if expected_size is not None and len(data) != expected_size:
        raise ValueError(
            f"{path}: expected {expected_size} bytes, found {len(data)}"
        )
    logging.info("%s: %d bytes, sha256=%s", path, len(data),
                 hashlib.sha256(data).hexdigest())
    return data


def identify(soc: AmlogicSoC) -> tuple[int, ...]:
    raw = soc.identify()
    identity = tuple(ord(value) for value in raw)
    if len(identity) != 8:
        raise RuntimeError(f"unexpected identify response: {identity!r}")
    logging.info(
        "ROM %d.%d, stage %d.%d, password-required=%d, password-ok=%d",
        *identity[:6],
    )
    return identity


def verify_memory(soc: AmlogicSoC, address: int, expected: bytes) -> None:
    """Read back a simple-memory upload before allowing its execution."""
    actual = bytearray()
    for offset in range(0, len(expected), 64):
        length = min(64, len(expected) - offset)
        actual.extend(soc.readSimpleMemory(address + offset, length))
    if actual != expected:
        mismatch = next(
            index for index, (left, right) in enumerate(zip(actual, expected))
            if left != right
        )
        raise RuntimeError(
            f"SRAM verification failed at {address + mismatch:#010x}: "
            f"read {actual[mismatch]:#04x}, expected {expected[mismatch]:#04x}"
        )
    logging.info("verified %d SRAM bytes at %#010x", len(expected), address)


def write_tpl_chunked(soc: AmlogicSoC, data: bytes) -> None:
    """Write TPL using bounded transactions to avoid long GXL USB stalls."""
    total = len(data)
    for offset in range(0, total, TPL_CHUNK_SIZE):
        chunk = data[offset:offset + TPL_CHUNK_SIZE]
        logging.info("loading TPL: %d/%d bytes", offset + len(chunk), total)
        soc.writeLargeMemory(
            UBOOT_LOAD + offset,
            chunk,
            blockLength=64,
            appendZeros=True,
        )


def load_ram_only(board_dir: Path, params_dir: Path) -> None:
    bl2 = read_checked(board_dir / "u-boot.bin.usb.bl2", 49152)
    tpl = read_checked(board_dir / "u-boot.bin.usb.tpl")
    ddr_params = read_checked(params_dir / "usbbl2runpara_ddrinit.bin", 32)
    fip_params = read_checked(params_dir / "usbbl2runpara_runfipimg.bin", 48)

    bl2_hash = hashlib.sha256(bl2).hexdigest()
    if bl2_hash != FACTORY_BL2_SHA256:
        raise ValueError(
            "refusing unknown BL2: expected the byte-identical P271 factory BL2"
        )

    soc = AmlogicSoC(timeout=0)
    initial_identity = identify(soc)
    if initial_identity[2:4] != (0, 0):
        raise RuntimeError(
            "device is not in the BootROM stage 0.0; refusing the BL2 write"
        )

    logging.info("loading BL2 to SRAM at %#010x", DDR_LOAD)
    soc.writeMemory(DDR_LOAD, bl2)
    verify_memory(soc, DDR_LOAD, bl2)
    soc.writeLargeMemory(BL2_PARAMS, ddr_params, blockLength=32)
    soc.run(DDR_LOAD)
    time.sleep(1)

    stage = identify(soc)[3]
    if stage == 8:
        logging.info("running DDR parameters at %#010x", BL2_PARAMS)
        soc.run(BL2_PARAMS)
        time.sleep(1)

    logging.info("probing initialized DDR at %#010x", UBOOT_LOAD)
    soc.writeMemory(UBOOT_LOAD, DDR_TEST_PATTERN)
    verify_memory(soc, UBOOT_LOAD, DDR_TEST_PATTERN)

    logging.info("loading the verified BL2/FIP set into volatile RAM")
    soc.writeLargeMemory(DDR_LOAD, bl2, blockLength=64)
    soc.writeLargeMemory(BL2_PARAMS, fip_params, blockLength=48)
    write_tpl_chunked(soc, tpl)

    entry = BL2_PARAMS if stage == 8 else DDR_LOAD
    logging.info("starting the RAM-only image at %#010x", entry)
    soc.run(entry)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Boot the P271 U-Boot image entirely from volatile RAM"
    )
    parser.add_argument("board_dir", type=Path)
    parser.add_argument("params_dir", type=Path)
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="[p271-ram] %(message)s")
    load_ram_only(args.board_dir, args.params_dir)


if __name__ == "__main__":
    main()

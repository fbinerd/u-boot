# Installation guide: GPL bootloader on the Intelbras SG 1002 MR L2+

Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>

SPDX-License-Identifier: GPL-2.0+

This guide is for whoever is going to install the GPL bootloader (Stage-1 +
Stage-2) on this switch, replacing the original proprietary factory
U-Boot. It assumes you already have basic experience soldering SMD
components and using an external SPI programmer (CH341A or equivalent).
It is not a beginner's guide to electronics.

**Before you start:** read `sg1002-mr.rst` and `sg1002-mr-findings.md` in
`doc/board/intelbras/` in the
<https://github.com/fbinerd/u-boot/tree/v2026.07-sg1002-mr> repository.
They explain what is verified on the board and what is still a known
limitation. This guide assumes that context.

## What you will need

- A soldering iron and experience with SMD rework (the SPI chip is
  soldered, not socketed).
- An external SPI programmer (CH341A or equivalent) and `flashrom`.
- A hex editor (HxD on Windows, or any other you prefer — `xxd`/`bless`/
  `ghex` on Linux work just as well).
- A 3.3 V TTL serial adapter to follow the boot.
- An Ethernet cable and a computer on the same network, for TFTP.
- The GPL bootloader already built from
  <https://github.com/fbinerd/u-boot/tree/v2026.07-sg1002-mr> (see that
  repository's `README`/`sg1002-mr.rst` for how to build it; the file you
  need here is `sg1002-mr-full-0x000000-0x04ffff.bin`).

## Physical safety warning: do not use a clip on the chip with the board assembled

**Do not attempt to program the SPI chip with a clip while it is soldered
in place and the board is powered or assembled.** On this board that
risks bus contention with the CPU and can kill the processor. The only
safe path is:

1. Desolder the SPI chip from the board.
2. Program it in isolation, on a bench, with the CH341A.
3. Only after it has been written and verified, resolder it onto the
   board.

Yes, this means doing this whole process again for every board — there is
no safe shortcut here.

## Step by step

### 1. Build the GPL bootloader

Follow the instructions in
<https://github.com/fbinerd/u-boot/tree/v2026.07-sg1002-mr> (branch
`v2026.07-sg1002-mr`). The file you will use is
`sg1002-mr-full-0x000000-0x04ffff.bin` — it already contains Stage-1 (the
DDR training) and Stage-2 (U-Boot itself) assembled in the correct
layout, ready to go into exactly the first 0x50000 bytes (320 KiB) of the
chip.

### 2. Desolder the SPI chip and make a complete backup of the factory contents

With the chip already off the board and in the programmer:

```
flashrom -p ch341a_spi -r factory_backup_read1.bin
flashrom -p ch341a_spi -r factory_backup_read2.bin
cmp factory_backup_read1.bin factory_backup_read2.bin && echo "reads are identical, the backup can be trusted"
```

Always do **two independent reads** and confirm they are byte for byte
identical before continuing. If they differ, the programmer/clip contact
is misaligned — reseat it and read again. Do not proceed with a backup
that did not match on both reads.

Keep this backup somewhere safe, with a name that leaves no doubt
(`factory_backup_read1.bin` is your only way back if something goes wrong
later). The chip is the full 16 MiB (16,777,216 bytes).

### 3. Edit the backup in the hex editor

Open `factory_backup_read1.bin` in HxD (or your editor of choice) and
replace the bytes from offset **`0x000000` through `0x04FFFF`** (320 KiB,
the first 0x50000 bytes) with the full contents of
`sg1002-mr-full-0x000000-0x04ffff.bin`.

- In HxD: open both files, select the entire contents of
  `sg1002-mr-full-0x000000-0x04ffff.bin`, copy it, and paste it over the
  start of the backup file (make sure the paste is "overwrite", not
  "insert" — the final file size must still be exactly 16,777,216 bytes).
- **Do not touch anything from `0x050000` onward** — that is where the
  original VxWorks bootrom lives, along with the factory data (MAC
  address) and the firmware partition. Keeping that intact is what
  guarantees the board can still fall back to OpenWrt or to the original
  firmware if needed.

Save it as a new file, for example `sg1002-mr-to-flash.bin`. Confirm it is
exactly 16,777,216 bytes before flashing it.

### 4. Flash it and resolder

```
flashrom -p ch341a_spi -w sg1002-mr-to-flash.bin
flashrom -p ch341a_spi -r verification.bin
cmp sg1002-mr-to-flash.bin verification.bin && echo "write confirmed"
```

Only after this confirmation, resolder the chip onto the board.

## What changes: the serial baud rate

The original factory firmware talks at **9600 bps** on the TTL serial,
from the very start of boot to the end (it never changes). The GPL
bootloader and OpenWrt raise that to **115200 bps**
(`CONFIG_BAUDRATE=115200`). After the swap, reconfigure your serial
adapter to 115200 — if you stay at 9600, you will see garbled, meaningless
text on the terminal through the whole new boot.

## Installing OpenWrt

With the GPL bootloader already working, the board still boots into the
original Intelbras system normally the first time (the firmware at
`0x150000` onward was never touched). From there, the expected path is:

1. **Through a web browser**, using Intelbras's own software's web
   interface (the original firmware already has `ip http server` enabled
   by default): reach the board at its IP and upload the OpenWrt image
   file through the original Intelbras system's own firmware-upgrade
   screen.

   *Warning: we did not test this path ourselves in this project — it is
   the expected path given that the original firmware's web upgrade
   interface exists, but we did not confirm in practice that it accepts
   an unmodified OpenWrt image. If it works, great; if not, use the path
   below, which we tested extensively.*

2. **If that fails**, install through U-Boot over the TTL serial + TFTP —
   this is the path we tested and used throughout this whole project:
   - connect the serial (already at 115200) and the Ethernet;
   - power on the board and interrupt autoboot at the `RTL838x#` prompt;
   - `tftp $loadaddr openwrt-sysupgrade.bin` (adjust the filename and the
     TFTP server IP to your own setup);
   - `sf probe 0`
   - `sf erase 0x150000 +<file size, in hex>`
   - `sf write $loadaddr 0x150000 <file size, in hex>`
   - `reset`

   See `sg1002-mr.rst` in `doc/board/intelbras/` in the GPL bootloader
   repository for the exact behavior of
   `boot_auto`/`openwrt_addr`/`vxworks_addr` and how U-Boot decides which
   system to start.

## If something goes wrong

Your complete factory backup (step 2) is the way back. If the board does
not boot after the swap, desolder the chip again, reflash the original,
unmodified backup, resolder it, and confirm it returns to factory
behavior before investigating what went wrong in the hex edit.

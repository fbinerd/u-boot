#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Build for the Intelbras SG1002 MR (RTL8380, GPL-only boot chain).
#
# Builds the three pieces from source and assembles ready-to-flash images:
#   Stage-1  the DDR-training preloader (arch/mips/mach-rtl-otto/preloader/),
#            a standalone assembly build.  DDR calibration is tied to this
#            board's DRAM chip and PCB, so it stays separate from U-Boot.
#   Stage-2  the GPL U-Boot, built from the standard defconfig.
#   env      the compiled-in default environment, encoded with mkenvimage.
#
# Stage-1 console output is off by default: -DSG1002_MR_PRELOADER_QUIET turns
# each byte-sending store into a same-size nop, so the layout, the delays and
# the timing stay those of the traced build.  SG1002_MR_STAGE1_TRACE=1 builds
# the traced variant from the current Stage-1 source.
#
# tools/check-sg1002-mr-repro.py compares a build with a known-good image.
#
# Outputs (all under $out, default build-gpl/sg1002-mr):
#   preloader.bin                    Stage-1 alone
#   u-boot.bin                       Stage-2 alone
#   env.bin                          compiled default environment
#                                     (board/intelbras/sg1002_mr/sg1002_mr.env
#                                     -> CONFIG_EXTRA_ENV_TEXT ->
#                                     u-boot-initial-env -> mkenvimage)
#   sg1002-mr-stage2-plus-env.bin    u-boot.bin + env.bin, 0x008000-0x04ffff:
#                                     the TFTP + sf self-flash image (Stage-1
#                                     untouched, no CH341A needed)
#   sg1002-mr-full-0x000000-0x04ffff.bin
#                                     preloader + stage2-plus-env, for CH341A
#                                     --region flashing.  Never covers
#                                     0x050000 onward (VxWorks, the vendor
#                                     filesystem and the OpenWrt image).
set -eu

: "${CROSS_COMPILE:?set CROSS_COMPILE to the MIPS big-endian toolchain prefix}"
out=${1:-build-gpl/sg1002-mr}

STAGE1_WINDOW=0x008000
STAGE2_OFFSET=0x008000
STAGE2_WINDOW=0x038000
ENV_SIZE=0x010000

export ARCH=mips

mkdir -p "$out"

# --- Stage-1: DDR-training preloader, standalone assembly build -----------
# The current GPL Stage-1 source is physically verified; the only remaining
# build switch is QUIET (see below).
src=arch/mips/mach-rtl-otto/preloader
s1="$out/preloader"
mkdir -p "$s1"
s1flags='-march=mips32r2 -mabi=32 -mno-abicalls -fno-pic -Iarch/mips/include'
if [ "${SG1002_MR_STAGE1_TRACE:-0}" != 1 ]; then
	s1flags="$s1flags -DSG1002_MR_PRELOADER_QUIET"
fi
"${CROSS_COMPILE}gcc" $s1flags -c "$src/sg1002_mr_nor.S" -o "$s1/nor.o"
"${CROSS_COMPILE}gcc" $s1flags -c "$src/sg1002_mr_sram.S" -o "$s1/sram.o"
"${CROSS_COMPILE}gcc" $s1flags -G0 -Os -ffreestanding -fno-builtin -fno-stack-protector \
	-c "$src/sg1002_mr_gpl_cali.c" -o "$s1/gpl-cali.o"
"${CROSS_COMPILE}ld" -EB -T "$src/sg1002_mr.lds" "$s1/nor.o" "$s1/sram.o" "$s1/gpl-cali.o" \
	-o "$s1/preloader.elf"
"${CROSS_COMPILE}objcopy" -O binary "$s1/preloader.elf" "$s1/preloader.bin"
sh "$(dirname "$0")/check-sg1002-mr-preloader.sh" "$s1/preloader.elf"
cp "$s1/preloader.bin" "$out/preloader.bin"
test "$(stat -c%s "$out/preloader.bin")" -le $((STAGE1_WINDOW))

# --- Stage-2: the real GPL U-Boot, from the standard defconfig ------------
make O="$out/uboot" intelbras_sg1002_mr_factory_payload_defconfig
./scripts/config --file "$out/uboot/.config" \
	-d TOOLS_LIBCRYPTO -d TOOLS_FIT_SIGNATURE -d TOOLS_RSASSA_PSS \
	-d TOOLS_KWBIMAGE -d FIT_SIGNATURE -d FIT_RSASSA_PSS
# SG1002_MR_LOCALVERSION replaces the git-derived "-g<hash>[-dirty]" suffix of
# the version banner with a fixed text (used by tools/check-sg1002-mr-repro.py,
# so that the result does not depend on the state of the git tree).
if [ -n "${SG1002_MR_LOCALVERSION:-}" ]; then
	./scripts/config --file "$out/uboot/.config" -d LOCALVERSION_AUTO \
		--set-str LOCALVERSION "$SG1002_MR_LOCALVERSION"
fi
make -j"$(nproc)" O="$out/uboot"
test -f "$out/uboot/u-boot.bin"
test "$(stat -c%s "$out/uboot/u-boot.bin")" -le $((STAGE2_WINDOW))
cp "$out/uboot/u-boot.bin" "$out/u-boot.bin"

# --- Compiled default environment, from that same build -------------------
# board/intelbras/sg1002_mr/sg1002_mr.env is baked into u-boot.bin as
# default_environment[]; dump it back out as text and encode that as the
# flashable partition, so the compiled-in fallback and the flashed partition
# cannot drift apart.
#
# printinitialenv is a host tool that bakes default_environment[] in at its own
# compile time and an incremental build does not rebuild it when only the .env
# changed, so delete it first and then check that every variable of the .env
# source made it into the dump.
find "$out/uboot/tools" -maxdepth 1 -name '*printinitialenv*' -delete
make O="$out/uboot" u-boot-initial-env
env_src=board/intelbras/sg1002_mr/sg1002_mr.env
for key in $(sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\)=.*/\1/p' "$env_src"); do
	grep -q "^$key=" "$out/uboot/u-boot-initial-env" || {
		printf '%s\n' "compiled env is missing $key from $env_src (stale printinitialenv?)" >&2
		exit 1
	}
done
make O="$out/uboot" envtools
"$out/uboot/tools/mkenvimage" -b -p 0x00 -s $((ENV_SIZE)) \
	-o "$out/env.bin" "$out/uboot/u-boot-initial-env"
test "$(stat -c%s "$out/env.bin")" -eq $((ENV_SIZE))

# --- Assemble the ready-to-flash images ------------------------------------
python3 - "$out/preloader.bin" "$out/u-boot.bin" "$out/env.bin" \
	"$out/sg1002-mr-stage2-plus-env.bin" \
	"$out/sg1002-mr-full-0x000000-0x04ffff.bin" <<'EOF'
import sys

preloader_path, ub_path, env_path, stage2_env_path, full_path = sys.argv[1:]

STAGE1_WINDOW = 0x008000
STAGE2_WINDOW = 0x038000
ENV_SIZE = 0x010000

preloader = open(preloader_path, 'rb').read()
ub = open(ub_path, 'rb').read()
env = open(env_path, 'rb').read()
assert len(preloader) <= STAGE1_WINDOW, f"preloader.bin too big: {len(preloader):#x}"
assert len(ub) <= STAGE2_WINDOW, f"u-boot.bin too big: {len(ub):#x}"
assert len(env) == ENV_SIZE, f"env.bin wrong size: {len(env):#x}"

stage1_padded = preloader + b'\xff' * (STAGE1_WINDOW - len(preloader))
stage2_padded = ub + b'\xff' * (STAGE2_WINDOW - len(ub))
stage2_plus_env = stage2_padded + env
assert len(stage2_plus_env) == STAGE2_WINDOW + ENV_SIZE
open(stage2_env_path, 'wb').write(stage2_plus_env)
print(f"{stage2_env_path}: {len(stage2_plus_env):#x} bytes "
      f"(u-boot.bin {len(ub):#x} padded to {STAGE2_WINDOW:#x} + env {ENV_SIZE:#x})")

full = stage1_padded + stage2_plus_env
assert len(full) == STAGE1_WINDOW + STAGE2_WINDOW + ENV_SIZE
open(full_path, 'wb').write(full)
print(f"{full_path}: {len(full):#x} bytes "
      f"(preloader.bin {len(preloader):#x} padded to {STAGE1_WINDOW:#x} "
      f"+ stage2+env {len(stage2_plus_env):#x}), "
      f"covers exactly 0x000000-{len(full)-1:06x}")
EOF

printf '%s\n' "preloader (Stage-1, from source): $out/preloader.bin"
printf '%s\n' "u-boot.bin (Stage-2, from source): $out/u-boot.bin"
printf '%s\n' "env.bin (compiled default env, from the same build): $out/env.bin"
printf '%s\n' "stage2+env (TFTP+sf self-flash, 0x008000-0x04ffff): $out/sg1002-mr-stage2-plus-env.bin"
printf '%s\n' "full image (CH341A --region 0x000000-0x04ffff only): $out/sg1002-mr-full-0x000000-0x04ffff.bin"

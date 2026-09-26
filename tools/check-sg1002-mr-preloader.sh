#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Static placement audit for the standalone RTL8380M NOR/SRAM preloader.
set -eu

: "${CROSS_COMPILE:?set CROSS_COMPILE to the MIPS big-endian toolchain prefix}"

image=${1:?usage: $0 PATH/TO/preloader.elf}
binary=${image%.elf}.bin
nm="${CROSS_COMPILE}nm"
readelf="${CROSS_COMPILE}readelf"

test -f "$image"
test -f "$binary"

size=$(stat -c%s "$binary")
test "$size" -le 32768 || {
	echo "preloader exceeds 32 KiB NOR/SRAM window: $size bytes" >&2
	exit 1
}

entry=$($readelf -h "$image" | awk '/Entry point address:/ { print $4 }')
test "$entry" = 0xbfc00000 || {
	echo "unexpected ELF entry point: $entry" >&2
	exit 1
}

symbol_address() {
	$nm -n "$image" | awk -v symbol="$1" '$3 == symbol { print "0x" $1; exit }'
}

check_symbol() {
	actual=$(symbol_address "$1")
	test "$actual" = "$2" || {
		echo "unexpected $1 address: ${actual:-missing}, expected $2" >&2
		exit 1
	}
}

check_symbol _start 0xbfc00000
check_symbol reset 0xbfc01000
check_symbol preloader_nor 0xbfc01040
check_symbol sram_entry 0xbf001200

$readelf -S "$image" | grep -q '[[:space:]]\.ucflash[[:space:]]' || exit 1
$readelf -S "$image" | grep -q '[[:space:]]\.sram[[:space:]]' || exit 1

echo "preloader placement OK: $size bytes, entry $entry"

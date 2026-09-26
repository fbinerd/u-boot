/* SPDX-License-Identifier: GPL-2.0+ */
/* Minimal NOR stage for the SG1002 MR GPL bring-up image. */

#include <asm/asm.h>
#include <asm/regdef.h>

	.set	noreorder
	.section .ucflash, "ax"
	.globl	_start

_start:
	/* The ROM walks four 0x400-byte vector copies before the loader. */
	b	vector_400
	 nop
	.org	0x400
vector_400:
	b	vector_800
	 nop
	.org	0x800
vector_800:
	b	vector_c00
	 nop
	.org	0xc00
vector_c00:
	b	reset
	 nop

	/* Match the factory loader's effective reset location at 0x1000. */
	.org	0x1000
reset:
	b	preloader_nor
	 nop

	.org	0x1040
preloader_nor:


	/* Enable and map the RTL8380 on-chip SRAM. */
	li	t0, 0xb8001300
	li	t1, 0x1f000001
	sw	t1, 0(t0)
	li	t2, 0x0000000a
	sw	t2, 4(t0)
	li	t0, 0xb8004000
	sw	t1, 0(t0)

	/* Copy the complete factory-compatible NOR/SRAM window. */
	li	t0, 0xbfc00000
	li	t1, 0xbf000000
	li	t2, 0x76bc
1:
	beqz	t2, 2f
	 nop
	lw	t3, 0(t0)
	sw	t3, 0(t1)
	addiu	t0, t0, 4
	addiu	t1, t1, 4
	addiu	t2, t2, -4
	b	1b
	 nop
2:
	/* Stay uncached: the persistent marker must not depend on cache setup. */
	li	t0, 0xbf001200
	jr	t0
	 nop

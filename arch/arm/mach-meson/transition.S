/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AArch32 kernel handoff for 64-bit Meson U-Boot.
 *
 * Copyright (C) 2026 Fabiano R. and contributors
 */

#include <linux/linkage.h>

/*
 * BL31 enters U-Boot at EL2.  The generic weak implementation returns to its
 * caller without changing execution state, which would execute the first ARM
 * instruction as AArch64.  Enter the 32-bit kernel at non-secure SVC/EL1 while
 * preserving the standard r0/r1/r2 Linux boot arguments already held in
 * x0/x1/x2.  Keep the EL2 setup compatible with Amlogic's GPL handoff:
 * notably HCR_EL2.HCD must remain clear because the legacy wrapper uses the
 * AArch32 virtualization interface while bringing up the kernel.
 */
ENTRY(armv8_el2_to_aarch32)
	mov	x5, x0
	mov	x6, x1
	mov	x7, x2

	/* AArch32 EL1 and EL0 are little-endian. */
	mrs	x8, sctlr_el2
	bic	x8, x8, #(1 << 25)
	msr	sctlr_el2, x8

	/* Set the architecturally required SCTLR_EL1 RES1 bits. */
	mov	x8, #0x0878
	movk	x8, #0x00c5, lsl #16
	msr	sctlr_el1, x8

	/* Allow EL1 physical timers and start with no virtual counter offset. */
	mrs	x8, cnthctl_el2
	orr	x8, x8, #3
	msr	cnthctl_el2, x8
	msr	cntvoff_el2, xzr

	/* Select AArch32 below EL2 and leave HVC enabled. */
	msr	hcr_el2, xzr
	isb

	/*
	 * VBAR_EL1 is never touched by BL31 or by this EL2 handoff, so it
	 * holds whatever the cold-reset/undefined value happens to be.  Any
	 * synchronous exception in the kernel's first few AArch32
	 * instructions (F/I/A above only mask async exceptions) would vector
	 * through that garbage address instead of somewhere debuggable.
	 * Mirror EL2's own vector table into EL1, matching what the generic
	 * armv8_switch_to_el1_m macro does for the same reason.
	 */
	mrs	x8, vbar_el2
	msr	vbar_el1, x8

	/* AArch32 SVC mode, ARM state, with asynchronous exceptions masked. */
	mov	x8, #0x1d3
	msr	spsr_el2, x8
	msr	elr_el2, x4

	mov	x0, x5
	mov	x1, x6
	mov	x2, x7
	eret
ENDPROC(armv8_el2_to_aarch32)

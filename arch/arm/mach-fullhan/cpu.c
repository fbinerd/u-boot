// SPDX-License-Identifier: GPL-2.0+
/*
 * Fullhan FH8626 SoC CPU, Reset and Timer control
 *
 * Copyright (C) 2026 OpenIPC / U-Boot Contributors
 */

#include <config.h>
#include <init.h>
#include <asm/io.h>

int print_cpuinfo(void)
{
	printf("CPU: Fullhan FH8626 (ARM1176JZF-S @ 500 MHz / ARMv6)\n");
	return 0;
}

void reset_cpu(void)
{
	/* Aciona watchdog / software reset em 0xF0100000 (CRG) */
	writel(0x1, (void *)0xF0100000);
	while (1)
		;
}

/* Timer para ARM1176 / ARMv6 */
unsigned long timer_read_counter(void)
{
	unsigned long val;
	/* Lê o Performance Monitor Cycle Counter do ARM1176 */
	asm volatile("mrc p15, 0, %0, c15, c12, 1" : "=r" (val));
	return val;
}

ulong get_tbclk(void)
{
	return CFG_SYS_HZ_CLOCK;
}

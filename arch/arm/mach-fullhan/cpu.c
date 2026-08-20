// SPDX-License-Identifier: GPL-2.0+
/*
 * Fullhan FH8626 SoC CPU & Reset control
 *
 * Copyright (C) 2026 OpenIPC / U-Boot Contributors
 */

#include <config.h>
#include <init.h>
#include <asm/io.h>

int print_cpuinfo(void)
{
	printf("CPU: Fullhan FH8626 (ARM Cortex-A7 @ 1.0 GHz)\n");
	return 0;
}

void reset_cpu(void)
{
	/* Aciona watchdog / software reset em 0xF0100000 (CRG) */
	writel(0x1, (void *)0xF0100000);
	while (1)
		;
}

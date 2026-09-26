// SPDX-License-Identifier: GPL-2.0+

#include <init.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int mach_cpu_init(void)
{
	return 0;
}

/* Placeholder until the RTL8380 DDR controller sequence is ported. */
void lowlevel_init(void)
{
}

int dram_init(void)
{
	/* Winbond W631GG8NB-12 is a 1 Gbit (128 MiB) DDR3 device. */
	/* RTL8380 DDR is reached through the cached MIPS KSEG0 alias. */
	gd->ram_base = 0x80000000;
	gd->ram_size = 0x08000000;
	return 0;
}

int print_cpuinfo(void)
{
	printf("Realtek RTL8380 OTTO (MIPS 4KEc)\n");
	return 0;
}

void _machine_restart(void)
{
	volatile u32 *wdt_ctrl = (void *)0xb8003158;

	/*
	 * Enable RTL8380 SoC watchdog reset with the shortest timeout
	 * (OTTO_WDT_CTRL_ENABLE | OTTO_WDT_MODE_SOC).
	 */
	*wdt_ctrl = 0x80000000;

	while (1)
		/* Wait for SoC reset */;
}

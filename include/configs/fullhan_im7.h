/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration settings for Intelbras Mibo iM7 / Dahua IPC-S21F (Fullhan FH8626)
 *
 * Copyright (C) 2026 OpenIPC / U-Boot Contributors
 */

#ifndef __CONFIG_FULLHAN_IM7_H
#define __CONFIG_FULLHAN_IM7_H

#include <linux/sizes.h>

#define CFG_SYS_SDRAM_BASE		0xa0000000
#define CFG_SYS_INIT_RAM_ADDR		0xa0800000
#define CFG_SYS_INIT_RAM_SIZE		0x40000
#define CFG_SYS_HZ_CLOCK		50000000
#define CFG_SYS_UBOOT_BASE		CONFIG_TEXT_BASE

/* Endereço de carga do Kernel e Ramdisk */
#define CFG_SYS_LOAD_ADDR		0xa0008000

/* Configurações de Boot e Partições SPI Flash */
#define CFG_EXTRA_ENV_SETTINGS \
	"bootargs=console=ttyS0,115200 root=/dev/mtdblock4 rootfstype=squashfs init=/linuxrc mem=48M\0" \
	"kernel_addr=0x70000\0" \
	"kernel_size=0x160000\0" \
	"loadaddr=0xa0008000\0" \
	"bootcmd=sf probe 0:0; sf read ${loadaddr} ${kernel_addr} ${kernel_size}; bootm ${loadaddr}\0"

#endif /* __CONFIG_FULLHAN_IM7_H */

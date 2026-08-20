// SPDX-License-Identifier: GPL-2.0+
/*
 * Board initialization for Intelbras Mibo iM7 / Dahua IPC-S21F (Fullhan FH8626)
 *
 * Copyright (C) 2026 OpenIPC / U-Boot Contributors
 */

#include <config.h>
#include <init.h>
#include <asm/io.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int board_init(void)
{
	/* Endereço de parâmetros de boot para o kernel */
	gd->bd->bi_boot_params = 0xa0000100;
	return 0;
}

int dram_init(void)
{
	/* 64 MB de memória DDR inicializada pelo 2BL Fullhan */
	gd->ram_size = 64 * 1024 * 1024;
	return 0;
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = 0xa0000000;
	gd->bd->bi_dram[0].size = gd->ram_size;
	return 0;
}

int checkboard(void)
{
	printf("Board: Intelbras Mibo iM7 / Dahua Imou IPC-S21F (Fullhan FH8626)\n");
	printf("TTL Console: Ativo e Desbloqueado (115200 8N1)\n");
	return 0;
}

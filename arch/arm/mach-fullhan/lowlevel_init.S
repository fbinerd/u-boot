/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Low-level initialization for Fullhan FH8626
 */

#include <config.h>
#include <linux/linkage.h>

ENTRY(lowlevel_init)
	mov	pc, lr
ENDPROC(lowlevel_init)

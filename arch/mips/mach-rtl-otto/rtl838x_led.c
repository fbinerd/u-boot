// SPDX-License-Identifier: GPL-2.0+
/*
 * RTL838x port LED engine, software control.
 *
 * Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>
 *
 * Register interface derived from the GPL Linux gpio-rtl8380-portled
 * driver of this project's OpenWrt tree, and measured on this hardware.
 */

#include <asm/addrspace.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <mach/rtl838x-led.h>

#define RTL838X_SW_BASE			CKSEG1ADDR(0x1b000000)

#define RTL838X_LED_MODE_SEL		0x1004
#define RTL838X_LED_GLB_CTRL		0xa000
#define RTL838X_LED_P_EN_CTRL		0xa008
#define RTL838X_LED_SW_CTRL		0xa00c
#define RTL838X_LED0_SW_P_EN_CTRL	0xa010
#define RTL838X_LED1_SW_P_EN_CTRL	0xa014
#define RTL838X_LED_SW_P_CTRL(port)	(0xa01c + ((port) << 2))

#define RTL838X_LED_MODE_SEL_MASK	GENMASK(1, 0)
#define RTL838X_LED_GLB_CTRL_MASK	GENMASK(5, 0)
/* Serial mode, two LED groups: 3 in the low field and 3 in the next one. */
#define RTL838X_LED_GLB_CTRL_SERIAL_2G	(0x3 | (0x3 << 3))
/* Per port and channel, 3 bits: 0 is off, 5 is what the Linux driver calls on. */
#define RTL838X_LED_SW_OFF		0x0
#define RTL838X_LED_SW_ON		0x5
#define RTL838X_LED_SW_MASK		0x7

static void sw_w32(u32 val, u32 reg)
{
	writel(val, (void __iomem *)(RTL838X_SW_BASE + reg));
}

static u32 sw_r32(u32 reg)
{
	return readl((void __iomem *)(RTL838X_SW_BASE + reg));
}

static void sw_w32_mask(u32 clear, u32 set, u32 reg)
{
	sw_w32((sw_r32(reg) & ~clear) | set, reg);
}

void rtl838x_port_led_init(u32 port_mask)
{
	u32 all = GENMASK(RTL838X_LED_PORTS - 1, 0);
	unsigned int port;

	sw_w32_mask(RTL838X_LED_MODE_SEL_MASK, 0, RTL838X_LED_MODE_SEL);
	sw_w32_mask(RTL838X_LED_GLB_CTRL_MASK, RTL838X_LED_GLB_CTRL_SERIAL_2G,
		    RTL838X_LED_GLB_CTRL);
	sw_w32(port_mask, RTL838X_LED_P_EN_CTRL);
	sw_w32(all, RTL838X_LED_SW_CTRL);
	sw_w32(all, RTL838X_LED0_SW_P_EN_CTRL);
	sw_w32(all, RTL838X_LED1_SW_P_EN_CTRL);

	for (port = 0; port < RTL838X_LED_PORTS; port++)
		sw_w32(0, RTL838X_LED_SW_P_CTRL(port));
}

void rtl838x_port_led_set(unsigned int port, unsigned int channel, bool on)
{
	unsigned int shift = channel * 3;

	if (port >= RTL838X_LED_PORTS || channel >= RTL838X_LED_CHANNELS)
		return;

	sw_w32_mask(RTL838X_LED_SW_MASK << shift,
		    (on ? RTL838X_LED_SW_ON : RTL838X_LED_SW_OFF) << shift,
		    RTL838X_LED_SW_P_CTRL(port));
}

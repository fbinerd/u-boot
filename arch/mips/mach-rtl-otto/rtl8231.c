// SPDX-License-Identifier: GPL-2.0+
/*
 * Realtek RTL8231 GPIO expander behind the RTL838x auxiliary MDIO bus.
 *
 * Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>
 *
 * Register interface derived from the GPL Linux drivers
 * (mdio-realtek-otto-aux, rtl8231 MFD and pinctrl) and the RTL8231
 * datasheet.  Kept deliberately small: the Stage-2 window has no room for
 * the driver-model GPIO/LED stack.
 */

#include <asm/addrspace.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <mach/rtl8231.h>

#define RTL838X_SW_BASE			CKSEG1ADDR(0x1b000000)

/*
 * SoC side.  Both bits are what the Linux devicetree selects with
 * pinctrl (mdio_aux_mdx / aux_mode_mdio): the aux pins are SoC GPIO2/GPIO3,
 * and they are plain GPIO (and the I2C pair) until these are set.  Without
 * them a command never completes: EXEC stays set.
 */
#define RTL838X_AUX_MODE_CTRL		0x0144		/* bit 0: MDIO (1) or I2C (0) */
#define RTL838X_AUX_PIN_CTRL		0xa0e0		/* bit 0: GPIO2/3 as MDC/MDIO */
#define RTL838X_EXT_GPIO_INDIRECT	0xa09c

#define AUX_MDIO_EXEC			BIT(0)
#define AUX_MDIO_WRITE			BIT(1)
#define AUX_MDIO_ADDR_SHIFT		2		/* 5 bits */
#define AUX_MDIO_REG_SHIFT		7		/* 5 bits */
#define AUX_MDIO_DATA_SHIFT		16
#define AUX_MDIO_TIMEOUT_US		1700		/* as the Linux driver */

#define RTL8231_REG_FUNC0		0x00
#define RTL8231_FUNC0_LED_START		BIT(1)
#define RTL8231_FUNC0_SYNC_GPIO		BIT(15)
#define RTL8231_REG_FUNC1		0x01
#define RTL8231_FUNC1_READY_MASK	GENMASK(9, 4)
#define RTL8231_FUNC1_READY_CODE	0x37
#define RTL8231_REG_PIN_MODE0		0x02
#define RTL8231_REG_PIN_MODE1		0x03
#define RTL8231_REG_PIN_HI_CFG		0x04
#define RTL8231_PIN_HI_MODE_MASK	GENMASK(4, 0)
#define RTL8231_PIN_HI_DIR_MASK		GENMASK(9, 5)
#define RTL8231_PIN_HI_SOFT_RESET	BIT(15)
#define RTL8231_REG_GPIO_DIR0		0x05
#define RTL8231_REG_GPIO_DIR1		0x06
#define RTL8231_REG_GPIO_DATA0		0x1c
#define RTL8231_REG_GPIO_DATA1		0x1d
#define RTL8231_REG_GPIO_DATA2		0x1e

/* In the mode registers 1 means GPIO, in the direction registers 1 means input. */

static int aux_mdio_cmd(unsigned int addr, unsigned int reg, u32 flags,
			u16 wdata, u16 *rdata)
{
	void __iomem *ctrl = (void __iomem *)(RTL838X_SW_BASE + RTL838X_EXT_GPIO_INDIRECT);
	u32 cmd = flags | AUX_MDIO_EXEC | (addr << AUX_MDIO_ADDR_SHIFT) |
		  (reg << AUX_MDIO_REG_SHIFT);
	unsigned int us;
	u32 val;

	if (flags & AUX_MDIO_WRITE)
		cmd |= (u32)wdata << AUX_MDIO_DATA_SHIFT;

	writel(cmd, ctrl);
	for (us = 0; us < AUX_MDIO_TIMEOUT_US; us += 3) {
		val = readl(ctrl);
		if (!(val & AUX_MDIO_EXEC)) {
			if (rdata)
				*rdata = val >> AUX_MDIO_DATA_SHIFT;
			return 0;
		}
		udelay(3);
	}
	return -ETIMEDOUT;
}

static int rd(unsigned int addr, unsigned int reg, u16 *val)
{
	return aux_mdio_cmd(addr, reg, 0, 0, val);
}

static int wr(unsigned int addr, unsigned int reg, u16 val)
{
	return aux_mdio_cmd(addr, reg, AUX_MDIO_WRITE, val, NULL);
}

static int update(unsigned int addr, unsigned int reg, u16 mask, u16 val)
{
	u16 v;
	int ret = rd(addr, reg, &v);

	if (ret)
		return ret;
	return wr(addr, reg, (v & ~mask) | (val & mask));
}

static void aux_mdio_enable(void)
{
	void __iomem *mode = (void __iomem *)(RTL838X_SW_BASE + RTL838X_AUX_MODE_CTRL);
	void __iomem *pins = (void __iomem *)(RTL838X_SW_BASE + RTL838X_AUX_PIN_CTRL);

	writel(readl(mode) | BIT(0), mode);
	writel(readl(pins) | BIT(0), pins);
}

int rtl8231_init(unsigned int addr)
{
	unsigned int us;
	u16 v;
	int ret;

	aux_mdio_enable();

	ret = rd(addr, RTL8231_REG_FUNC1, &v);
	if (ret)
		return ret;
	/* A bus nobody drives reads 0xffff, which masks to 0x3f, not 0x37. */
	if (((v & RTL8231_FUNC1_READY_MASK) >> 4) != RTL8231_FUNC1_READY_CODE)
		return -ENODEV;

	ret = rd(addr, RTL8231_REG_FUNC0, &v);
	if (ret)
		return ret;
	if (v & RTL8231_FUNC0_LED_START)
		goto enable_immediate_gpio;	/* keep the running pin state */

	/* Cold chip: soft reset (self-clearing), then every pin a GPIO input. */
	ret = update(addr, RTL8231_REG_PIN_HI_CFG, RTL8231_PIN_HI_SOFT_RESET,
		     RTL8231_PIN_HI_SOFT_RESET);
	for (us = 0; !ret && us < 1000; us += 50) {
		ret = rd(addr, RTL8231_REG_PIN_HI_CFG, &v);
		if (!ret && !(v & RTL8231_PIN_HI_SOFT_RESET))
			break;
		udelay(50);
	}
	if (ret)
		return ret;
	if (v & RTL8231_PIN_HI_SOFT_RESET)
		return -ETIMEDOUT;

	/* The reset leaves a mix of LED and GPIO outputs: inputs first. */
	ret = wr(addr, RTL8231_REG_PIN_MODE0, 0xffff);
	if (!ret)
		ret = wr(addr, RTL8231_REG_GPIO_DIR0, 0xffff);
	if (!ret)
		ret = wr(addr, RTL8231_REG_PIN_MODE1, 0xffff);
	if (!ret)
		ret = wr(addr, RTL8231_REG_GPIO_DIR1, 0xffff);
	if (!ret)
		ret = wr(addr, RTL8231_REG_PIN_HI_CFG,
			 RTL8231_PIN_HI_MODE_MASK | RTL8231_PIN_HI_DIR_MASK);
	/* LED_START powers the pin drivers. */
	if (!ret)
		ret = update(addr, RTL8231_REG_FUNC0, RTL8231_FUNC0_LED_START,
				     RTL8231_FUNC0_LED_START);
	if (ret)
		return ret;

enable_immediate_gpio:
	/* A prior firmware may have selected latched GPIO updates. */
	return update(addr, RTL8231_REG_FUNC0, RTL8231_FUNC0_SYNC_GPIO, 0);
}

int rtl8231_gpio_set_output(unsigned int addr, unsigned int pin, int value)
{
	unsigned int mode_reg, dir_reg, data_reg;
	u16 mode_mask, dir_mask, data_mask, data;
	int ret;

	if (pin >= RTL8231_NGPIO)
		return -EINVAL;

	if (pin < 32) {
		unsigned int bit = pin % 16;

		mode_reg = RTL8231_REG_PIN_MODE0 + pin / 16;
		dir_reg = RTL8231_REG_GPIO_DIR0 + pin / 16;
		data_reg = RTL8231_REG_GPIO_DATA0 + pin / 16;
		mode_mask = dir_mask = data_mask = BIT(bit);
	} else {
		/* Pins 32..36: mode and direction live in PIN_HI_CFG. */
		unsigned int bit = pin - 32;

		mode_reg = dir_reg = RTL8231_REG_PIN_HI_CFG;
		data_reg = RTL8231_REG_GPIO_DATA2;
		mode_mask = BIT(bit);
		dir_mask = BIT(bit + 5);
		data_mask = BIT(bit);
	}

	ret = update(addr, mode_reg, mode_mask, mode_mask);
	if (ret)
		return ret;

	/*
	 * Reading the data register returns the pin levels (the driven level
	 * for outputs), writing it sets the output latches: write back what
	 * the other output pins are already driving.
	 */
	ret = rd(addr, data_reg, &data);
	if (ret)
		return ret;
	data &= ~data_mask;
	if (value)
		data |= data_mask;
	ret = wr(addr, data_reg, data);
	if (ret)
		return ret;
	return update(addr, dir_reg, dir_mask, 0);
}

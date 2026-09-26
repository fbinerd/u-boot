// SPDX-License-Identifier: GPL-2.0+
/*
 * RTL8380 boot SPI controller, single-bit PIO mode.
 *
 * The controller transfers one to four bytes through its data register.  A
 * byte-at-a-time implementation is intentional here: it keeps SPI command,
 * address, status and page-program transactions under one explicit chip
 * select and is sufficient for recovery flashing a W25Q128.
 */

#include <dm.h>
#include <spi.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <asm/io.h>

#define RTL8380_SPIF_CFG		0x00
#define RTL8380_SPIF_CTRL		0x08
#define RTL8380_SPIF_DATA		0x0c

#define RTL8380_SPIF_CTRL_CS0_INACTIVE	BIT(31)
#define RTL8380_SPIF_CTRL_CS1_INACTIVE	BIT(30)
#define RTL8380_SPIF_CTRL_READY		BIT(27)
#define RTL8380_SPIF_CTRL_LEN(n)	(((n) == 4 ? 0x03 : 0x00) << 28)
#define RTL8380_SPIF_CTRL_LEN_MASK	(~(0x03 << 28))
#define RTL8380_SPIF_CTRL_CS		BIT(24)
#define RTL8380_SPIF_CFG_READ_BYTE_ORDER	BIT(28)
#define RTL8380_SPIF_CFG_WRITE_BYTE_ORDER	BIT(27)
#define RTL8380_SPIF_CFG_TCS		(0x1f << 22)
#define RTL8380_SPIF_CFG2		0x04
#define RTL8380_SPIF_CFG2_CMD(cmd)	((cmd) << 24)
#define RTL8380_SPIF_CFG2_SIZE_MASK	(0x7 << 21)
#define RTL8380_SPIF_CFG2_RDOPT	BIT(20)
#define RTL8380_SPIF_CFG2_DUMMY(n)	((n) << 13)
#define RTL8380_SPIF_CFG2_HOLD	BIT(10)

struct rtl8380_spi_priv {
	void __iomem *base;
};

static int rtl8380_spi_wait_ready(struct rtl8380_spi_priv *priv)
{
	unsigned int timeout;

	for (timeout = 0; timeout < 10000; timeout++) {
		if (__raw_readl(priv->base + RTL8380_SPIF_CTRL) &
		    RTL8380_SPIF_CTRL_READY)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static void rtl8380_spi_activate(struct rtl8380_spi_priv *priv)
{
	u32 ctrl = __raw_readl(priv->base + RTL8380_SPIF_CTRL);

	/* CS0 bit (CSB0) is active low */
	ctrl &= ~RTL8380_SPIF_CTRL_CS0_INACTIVE;
	__raw_writel(ctrl, priv->base + RTL8380_SPIF_CTRL);
}

static void rtl8380_spi_deactivate(struct rtl8380_spi_priv *priv)
{
	u32 ctrl = __raw_readl(priv->base + RTL8380_SPIF_CTRL);

	/* CS0 bit (CSB0) is active low: set bit 31 to de-assert */
	ctrl |= RTL8380_SPIF_CTRL_CS0_INACTIVE;
	__raw_writel(ctrl, priv->base + RTL8380_SPIF_CTRL);
}

static void rtl8380_spi_set_size(struct rtl8380_spi_priv *priv,
				 unsigned int bytes)
{
	u32 ctrl = __raw_readl(priv->base + RTL8380_SPIF_CTRL);

	ctrl &= RTL8380_SPIF_CTRL_LEN_MASK;
	ctrl |= RTL8380_SPIF_CTRL_LEN(bytes);
	__raw_writel(ctrl, priv->base + RTL8380_SPIF_CTRL);
}

static int rtl8380_spi_xfer_byte(struct rtl8380_spi_priv *priv, u8 out, u8 *in)
{
	int ret;

	ret = rtl8380_spi_wait_ready(priv);
	if (ret)
		return ret;
	rtl8380_spi_set_size(priv, 1);
	__raw_writel((u32)out << 24, priv->base + RTL8380_SPIF_DATA);
	ret = rtl8380_spi_wait_ready(priv);
	if (ret)
		return ret;
	if (in)
		*in = __raw_readl(priv->base + RTL8380_SPIF_DATA) >> 24;

	return 0;
}

/*
 * RX is controller-driven: after an opcode/address phase the RTL8380 clocks
 * data when SFCSR length is written and presents up to four bytes in SFDR.
 */
static int rtl8380_spi_receive(struct rtl8380_spi_priv *priv, u8 *rx,
			       unsigned int bytes)
{
	unsigned int i, count;
	u32 data;
	int ret;

	while (bytes) {
		count = (bytes >= 4) ? 4 : 1;
		ret = rtl8380_spi_wait_ready(priv);
		if (ret)
			return ret;
		rtl8380_spi_set_size(priv, count);
		ret = rtl8380_spi_wait_ready(priv);
		if (ret)
			return ret;
		data = __raw_readl(priv->base + RTL8380_SPIF_DATA);
		if (count == 4) {
			for (i = 0; i < 4; i++)
				rx[i] = data >> (24 - i * 8);
		} else {
			rx[0] = data >> 24;
		}
		rx += count;
		bytes -= count;
	}

	return 0;
}

static int rtl8380_spi_xfer(struct udevice *dev, unsigned int bitlen,
			    const void *dout, void *din, unsigned long flags)
{
	struct rtl8380_spi_priv *priv = dev_get_priv(dev->parent);
	struct dm_spi_slave_plat *plat = dev_get_parent_plat(dev);
	const u8 *tx = dout;
	u8 *rx = din;
	unsigned int i, bytes;
	int ret = 0;

	if (bitlen & 7) {
		ret = -EINVAL;
		goto out;
	}
	if (plat->cs[0]) {
		ret = -ENODEV;
		goto out;
	}
	bytes = bitlen / 8;

	if (flags & SPI_XFER_BEGIN) {
		rtl8380_spi_activate(priv);
		ret = rtl8380_spi_wait_ready(priv);
		if (ret)
			goto out;
	}

	if (!tx && rx)
		ret = rtl8380_spi_receive(priv, rx, bytes);
	else
		for (i = 0; i < bytes; i++) {
			ret = rtl8380_spi_xfer_byte(priv, tx ? tx[i] : 0,
					     rx ? &rx[i] : NULL);
			if (ret)
				break;
		}

out:
	/* An error also ends the transaction; do not leave CS0 asserted. */
	if (ret || (flags & SPI_XFER_END)) {
		rtl8380_spi_deactivate(priv);
		if (!ret)
			ret = rtl8380_spi_wait_ready(priv);
	}

	return ret;
}

static int rtl8380_spi_set_speed(struct udevice *bus, uint speed)
{
	/* The boot controller's divider is fixed for this recovery-only driver. */
	return 0;
}

static int rtl8380_spi_set_mode(struct udevice *bus, uint mode)
{
	/* The boot NOR is wired for standard SPI mode 0. */
	return mode ? -EINVAL : 0;
}

static int rtl8380_spi_probe(struct udevice *bus)
{
	struct rtl8380_spi_priv *priv = dev_get_priv(bus);
	u32 ctrl;

	priv->base = dev_remap_addr(bus);
	if (!priv->base)
		return -EINVAL;
	__raw_writel(RTL8380_SPIF_CFG_TCS |
		     RTL8380_SPIF_CFG_READ_BYTE_ORDER |
		     RTL8380_SPIF_CFG_WRITE_BYTE_ORDER |
		     (__raw_readl(priv->base + RTL8380_SPIF_CFG) &
		      ~(RTL8380_SPIF_CFG_TCS |
			RTL8380_SPIF_CFG_READ_BYTE_ORDER |
			RTL8380_SPIF_CFG_WRITE_BYTE_ORDER)),
		     priv->base + RTL8380_SPIF_CFG);

	/* Permanently disable CS1 and select CS0; leave CS0 de-asserted (CSB0=1). */
	ctrl = __raw_readl(priv->base + RTL8380_SPIF_CTRL);
	ctrl |= RTL8380_SPIF_CTRL_CS0_INACTIVE | RTL8380_SPIF_CTRL_CS1_INACTIVE;
	ctrl &= ~RTL8380_SPIF_CTRL_CS;
	__raw_writel(ctrl, priv->base + RTL8380_SPIF_CTRL);
	return 0;
}

static const struct dm_spi_ops rtl8380_spi_ops = {
	.xfer = rtl8380_spi_xfer,
	.set_speed = rtl8380_spi_set_speed,
	.set_mode = rtl8380_spi_set_mode,
};

static const struct udevice_id rtl8380_spi_ids[] = {
	{ .compatible = "realtek,rtl8380-spi" },
	{ }
};

U_BOOT_DRIVER(rtl8380_spi) = {
	.name = "rtl8380_spi",
	.id = UCLASS_SPI,
	.of_match = rtl8380_spi_ids,
	.ops = &rtl8380_spi_ops,
	.priv_auto = sizeof(struct rtl8380_spi_priv),
	.probe = rtl8380_spi_probe,
};

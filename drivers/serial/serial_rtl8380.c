// SPDX-License-Identifier: GPL-2.0+
/* Minimal driver-model UART support for Realtek RTL8380 switch SoCs. */

#include <dm.h>
#include <errno.h>
#include <serial.h>
#include <asm/io.h>

#define RTL8380_UART_RBR	0
#define RTL8380_UART_THR	0
#define RTL8380_UART_IER	1
#define RTL8380_UART_FCR	2
#define RTL8380_UART_LCR	3
#define RTL8380_UART_LSR	5

#define RTL8380_UART_LCR_DLAB	0x80
#define RTL8380_UART_LCR_8N1	0x03
#define RTL8380_UART_LSR_DR	0x01
#define RTL8380_UART_LSR_THRE	0x20

struct rtl8380_uart_priv {
	void __iomem *base;
	u32 clock;
};

static void __iomem *rtl8380_uart_reg(struct rtl8380_uart_priv *priv,
					      unsigned int reg)
{
	/* Each NS16550 register occupies a big-endian 32-bit word. */
	return priv->base + (reg << 2);
}

static u8 rtl8380_uart_read(struct rtl8380_uart_priv *priv, unsigned int reg)
{
	return readl(rtl8380_uart_reg(priv, reg)) >> 24;
}

static void rtl8380_uart_write(struct rtl8380_uart_priv *priv,
				       unsigned int reg, u8 value)
{
	writel((u32)value << 24, rtl8380_uart_reg(priv, reg));
}

static int rtl8380_uart_setbrg(struct udevice *dev, int baudrate)
{
	struct rtl8380_uart_priv *priv = dev_get_priv(dev);
	u32 divisor;

	/*
	 * The rate is honoured, but a non-default baudrate is a known hazard: of
	 * seven resets tested with 9600 saved, six booted and one stopped producing
	 * output near this transition until a power cycle.  That small sample does
	 * not identify the cause or a failure rate.  The compiled-in default and
	 * saved environment stay at 115200, and on_baudrate() does not call back
	 * in when the value is unchanged, so a default boot never gets here twice.
	 *
	 * Only Stage-2's own output follows this rate: Stage-1 always runs at 115200,
	 * and Linux does not inherit the divisor (its earlycon reprograms the UART
	 * at 115200).
	 */
	if (!baudrate || !priv->clock)
		return -EINVAL;

	divisor = DIV_ROUND_CLOSEST(priv->clock, 16 * baudrate);
	/* Program the divisor latch: with DLAB set, offsets 0 and 1 are DLL and DLM. */
	rtl8380_uart_write(priv, RTL8380_UART_LCR,
			   RTL8380_UART_LCR_DLAB | RTL8380_UART_LCR_8N1);
	rtl8380_uart_write(priv, RTL8380_UART_RBR, divisor & 0xff);
	rtl8380_uart_write(priv, RTL8380_UART_IER, divisor >> 8);
	rtl8380_uart_write(priv, RTL8380_UART_LCR, RTL8380_UART_LCR_8N1);
	/*
	 * Re-mask interrupts after every setbrg(): the divisor's high byte went
	 * through IER (DLM) while DLAB was set, and it must not leak through as
	 * interrupt-enable bits once DLAB clears.
	 */
	rtl8380_uart_write(priv, RTL8380_UART_IER, 0);

	return 0;
}

static int rtl8380_uart_probe(struct udevice *dev)
{
	struct rtl8380_uart_priv *priv = dev_get_priv(dev);
	fdt_addr_t addr;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE) {
		return -EINVAL;
	}
	priv->base = map_physmem(addr, 0x100, MAP_NOCACHE);
	priv->clock = dev_read_u32_default(dev, "clock-frequency", 200000000);
	rtl8380_uart_write(priv, RTL8380_UART_IER, 0);
	rtl8380_uart_write(priv, RTL8380_UART_FCR, 7);

	return rtl8380_uart_setbrg(dev,
		dev_read_u32_default(dev, "current-speed", 115200));
}

static int rtl8380_uart_putc(struct udevice *dev, const char ch)
{
	struct rtl8380_uart_priv *priv = dev_get_priv(dev);

	if (!(rtl8380_uart_read(priv, RTL8380_UART_LSR) & RTL8380_UART_LSR_THRE))
		return -EAGAIN;
	rtl8380_uart_write(priv, RTL8380_UART_THR, ch);

	return 0;
}

static int rtl8380_uart_getc(struct udevice *dev)
{
	struct rtl8380_uart_priv *priv = dev_get_priv(dev);

	if (!(rtl8380_uart_read(priv, RTL8380_UART_LSR) & RTL8380_UART_LSR_DR))
		return -EAGAIN;

	return rtl8380_uart_read(priv, RTL8380_UART_RBR);
}

static int rtl8380_uart_pending(struct udevice *dev, bool input)
{
	struct rtl8380_uart_priv *priv = dev_get_priv(dev);
	u8 lsr = rtl8380_uart_read(priv, RTL8380_UART_LSR);

	return input ? !!(lsr & RTL8380_UART_LSR_DR) : !(lsr & RTL8380_UART_LSR_THRE);
}

static const struct dm_serial_ops rtl8380_uart_ops = {
	.putc	= rtl8380_uart_putc,
	.getc	= rtl8380_uart_getc,
	.pending = rtl8380_uart_pending,
	.setbrg = rtl8380_uart_setbrg,
};

static const struct udevice_id rtl8380_uart_ids[] = {
	{ .compatible = "realtek,rtl8380-uart" },
	{}
};

U_BOOT_DRIVER(rtl8380_uart) = {
	.name		= "rtl8380_uart",
	.id		= UCLASS_SERIAL,
	.of_match	= rtl8380_uart_ids,
	.probe		= rtl8380_uart_probe,
	.priv_auto	= sizeof(struct rtl8380_uart_priv),
	.ops		= &rtl8380_uart_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};

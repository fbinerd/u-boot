// SPDX-License-Identifier: GPL-2.0+
/*
 * Minimal polling Ethernet driver for the RTL8380 CPU port.
 *
 * The switch ASIC owns the MAC. Frames reach the MIPS CPU through its DMA
 * interface and CPU port 28. This small U-Boot driver uses RX/TX DMA rings
 * for recovery networking without an SDK binary or a PHY framework.
 *
 * Register definitions and descriptor layout were independently implemented
 * from the GPL Linux rtl838x Ethernet driver and RTL838x documentation.
 */

#include <cpu_func.h>
#include <dm.h>
#include <malloc.h>
#include <net.h>
#include <asm/addrspace.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <mach/rtl838x-eth.h>

#define RTL838X_CPU_PORT 28
#define RTL838X_RST_GLB_CTRL 0x003c
#define RTL838X_QM_PKT2CPU_MAP 0x5f10
#define RTL838X_QM_RSN2CPUQ 0x5f04
#define RTL838X_PORT_ISO_CTRL(port) (0x4100 + (port) * 4)
#define RTL838X_RX_FILTER 0x6b10
#define RTL838X_TX_FILTER 0xaa6c
#define RTL838X_MAC_ADDR_CTRL 0xa9ec
#define RTL838X_MAC_ADDR_CTRL_ALE 0x6b04
#define RTL838X_MAC_ADDR_CTRL_MAC 0xa320
#define RTL838X_DMA_RX_BASE 0x9f00
#define RTL838X_DMA_TX_BASE 0x9f40
#define RTL838X_DMA_INTR_MSK 0x9f50
#define RTL838X_DMA_INTR_STS 0x9f54
#define RTL838X_DMA_CTRL 0x9f58
#define RTL838X_DMA_RX_RING_SIZE 0xb7e4
#define RTL838X_CPU_FORCE_MODE (0xa104 + RTL838X_CPU_PORT * 4)
#define RTL838X_CPU_L2_PORT_CTRL (0xd560 + RTL838X_CPU_PORT * 128)
#define RTL838X_MAC_PORT_CTRL(port) (0xd560 + (port) * 128)
#define RTL838X_MAC_LINK_STS 0xa188
#define RTL838X_MAC_LINK_SPD_STS 0xa190
#define RTL838X_STAT_PORT_STD_MIB 0x1200
#define RTL838X_SMI_GLB_CTRL 0xa100
#define RTL838X_SMI_ACCESS_PHY_CTRL_0 0xa1b8
#define RTL838X_SMI_ACCESS_PHY_CTRL_1 0xa1bc
#define RTL838X_SMI_ACCESS_PHY_CTRL_2 0xa1c0
#define RTL838X_SMI_ACCESS_PHY_CTRL_3 0xa1c4
#define RTL838X_SMI_POLL_CTRL 0xa17c
#define RTL838X_SMI_PORT_ADDR_CTRL(port) (0xa1c8 + ((port) / 6) * 4)
#define RTL838X_SMI_PORT_ADDR_SHIFT(port) (((port) % 6) * 5)

#define RTL838X_RING_OWN BIT(0)
#define RTL838X_RING_WRAP BIT(1)
#define RTL838X_DMA_ENABLE 0x0000000c
#define RTL838X_DMA_TRIGGER_TX BIT(1)
#define RTL838X_DMA_TX_PAD BIT(5)
#define RTL838X_PKT_LEN 1536
/*
 * The DMA engine can prefetch several descriptors.  Four entries can drain
 * before U-Boot's polling TFTP client returns ownership, which showed up as
 * a transfer stopping after the first few DATA blocks on real hardware.
 */
#define RTL838X_RX_COUNT 32
#define RTL838X_COPPER_PORT_FIRST 8
#define RTL838X_COPPER_PORT_LAST 15
#define RTL838X_COPPER_PORT_MASK GENMASK(RTL838X_COPPER_PORT_LAST, RTL838X_COPPER_PORT_FIRST)
#define RTL838X_SMI_RUN BIT(0)
#define RTL838X_SMI_WRITE_C22 BIT(2)
#define RTL838X_SMI_PHY_PATCH_DONE BIT(15)
#define RTL838X_PHY_BMCR_AN_RESTART 0x1200
#define RTL838X_PHY_BMSR_LINK_STATUS BIT(2)

/*
 * OpenWrt Linux GPL drivers/net/pcs/pcs-rtl-otto.c:
 * RTPCS_838X_MAC_LINK_SPD_STS at 0xa190, two speed bits per MAC port.
 */
#define RTL838X_MAC_SPEED_1000 2
#define RTL838X_SFP_PORT_MASK (BIT(24) | BIT(26))

struct rtl838x_frag {
	u32 dma;
	u16 reserved;
	u16 size;
	u16 more_offset;
	u16 len;
	u16 cpu_tag[10];
} __packed;

struct rtl838x_priv {
	void __iomem *base;
	void *rx_ring_mem, *tx_ring_mem, *rx_buf_mem, *tx_buf_mem;
	u32 *rx_ring, *tx_ring;
	struct rtl838x_frag *rx_frag, *tx_frag;
	uchar *rx_buf, *tx_buf;
	unsigned int rx_slot;
	u32 tx_port_mask;
};

static inline void rtl_writel(struct rtl838x_priv *priv, u32 value, u32 reg)
{
	writel(value, priv->base + reg);
}

static inline u32 rtl_readl(struct rtl838x_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static void *rtl_uncached(void *ptr)
{
	return (void *)CKSEG1ADDR(virt_to_phys(ptr));
}

/*
 * RTL8380 internal PHY access.  This is the documented SMI command sequence
 * used by the GPL Linux Otto MDIO driver: select page/register, submit, then
 * wait for the RUN bit to clear.  Recovery only needs page 0/BMCR.
 */
static int rtl838x_phy_write_c22(struct rtl838x_priv *priv, int port, int reg, u16 value)
{
	unsigned int timeout;
	u32 command = (reg << 20) | (0x1f << 15) | RTL838X_SMI_WRITE_C22 |
		RTL838X_SMI_RUN;

	rtl_writel(priv, BIT(port), RTL838X_SMI_ACCESS_PHY_CTRL_0);
	rtl_writel(priv, (u32)value << 16, RTL838X_SMI_ACCESS_PHY_CTRL_2);
	rtl_writel(priv, 0, RTL838X_SMI_ACCESS_PHY_CTRL_3);
	rtl_writel(priv, command, RTL838X_SMI_ACCESS_PHY_CTRL_1);
	for (timeout = 0; timeout < 5000; timeout++) {
		if (!(rtl_readl(priv, RTL838X_SMI_ACCESS_PHY_CTRL_1) & RTL838X_SMI_RUN))
			return 0;
		udelay(20);
	}
	return -ETIMEDOUT;
}

/*
 * The SMI command engine addresses MAC ports, not PHY pins directly.  Its
 * five-bit MAC-port -> SMI-address map must be initialized before issuing a
 * C22 command.  The RTL8380M_INTPHY_2FIB_1G_DEMO internal RTL8218 package
 * uses identity mapping
 * for MAC/PHY 8..15.
 */
static void rtl838x_phy_set_smi_address(struct rtl838x_priv *priv, int port)
{
	u32 reg = RTL838X_SMI_PORT_ADDR_CTRL(port);
	u32 shift = RTL838X_SMI_PORT_ADDR_SHIFT(port);
	u32 value = rtl_readl(priv, reg);

	value &= ~(GENMASK(4, 0) << shift);
	value |= (u32)port << shift;
	rtl_writel(priv, value, reg);
}

/*
 * RTL8380 C22 reads use the same command engine as writes, but select one
 * PHY address in CTRL_2 bits 31:16 and return the result through CTRL_2
 * bits 15:0.
 * BMSR is read twice by the caller because its link bit is latch-low.
 */
static int rtl838x_phy_read_c22(struct rtl838x_priv *priv, int port, int reg,
				u16 *value)
{
	unsigned int timeout;
	u32 command = (reg << 20) | (0x1f << 15) | RTL838X_SMI_RUN;

	rtl_writel(priv, 0, RTL838X_SMI_ACCESS_PHY_CTRL_0);
	rtl_writel(priv, (u32)port << 16, RTL838X_SMI_ACCESS_PHY_CTRL_2);
	rtl_writel(priv, 0, RTL838X_SMI_ACCESS_PHY_CTRL_3);
	rtl_writel(priv, command, RTL838X_SMI_ACCESS_PHY_CTRL_1);
	for (timeout = 0; timeout < 5000; timeout++) {
		if (!(rtl_readl(priv, RTL838X_SMI_ACCESS_PHY_CTRL_1) & RTL838X_SMI_RUN)) {
			*value = rtl_readl(priv, RTL838X_SMI_ACCESS_PHY_CTRL_2) & 0xffff;
			return 0;
		}
		udelay(20);
	}
	return -ETIMEDOUT;
}

static int rtl838x_enable_copper_ports(struct udevice *dev)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	u32 cpu_mask = BIT(RTL838X_CPU_PORT);
	u32 copper_mask = GENMASK(RTL838X_COPPER_PORT_LAST, RTL838X_COPPER_PORT_FIRST);
	u16 bmsr, phy_id1, phy_id2;
	int port, ret;

	/* The RTL8380M demo layout has eight internal copper PHYs on ports 8..15. */
	rtl_writel(priv, rtl_readl(priv, RTL838X_SMI_GLB_CTRL) |
		    RTL838X_SMI_PHY_PATCH_DONE, RTL838X_SMI_GLB_CTRL);
	for (port = RTL838X_COPPER_PORT_FIRST; port <= RTL838X_COPPER_PORT_LAST; port++) {
		rtl838x_phy_set_smi_address(priv, port);
		ret = rtl838x_phy_write_c22(priv, port, 0, RTL838X_PHY_BMCR_AN_RESTART);
		if (ret) {
			printf("rtl8380: PHY %d SMI timeout\n", port);
			return ret;
		}
		rtl_writel(priv, rtl_readl(priv, RTL838X_MAC_PORT_CTRL(port)) | 0x3,
			    RTL838X_MAC_PORT_CTRL(port));
		/* Isolate recovery traffic to CPU port <-> this physical port. */
		rtl_writel(priv, cpu_mask, RTL838X_PORT_ISO_CTRL(port));
	}
	rtl_writel(priv, cpu_mask | copper_mask, RTL838X_PORT_ISO_CTRL(RTL838X_CPU_PORT));
	rtl_writel(priv, rtl_readl(priv, RTL838X_SMI_POLL_CTRL) | copper_mask,
		    RTL838X_SMI_POLL_CTRL);
	/*
	 * Recovery traffic goes out every copper port's bit in the direct-port-mask
	 * CPU tag, not just whichever port(s) happen to be linked yet: ping,
	 * tftpboot and dhcp then work the same way regardless of which physical
	 * port the cable is on, or how many are connected, with no wait to find
	 * out which -- the switch's own port isolation (above) already keeps this
	 * from also bridging the copper ports to each other. See F04, F25 and
	 * F26 in doc/board/intelbras/sg1002-mr-findings.md: two different ways
	 * that picking a single "linked" port from a still-settling snapshot went
	 * wrong; this sends everywhere instead of picking, so there is nothing
	 * left to get wrong from autonegotiation timing. This was in fact the
	 * original design, before an earlier change narrowed it down to fight
	 * the same problem a second time.
	 */
	priv->tx_port_mask = copper_mask;
	for (port = RTL838X_COPPER_PORT_FIRST;
	     port <= RTL838X_COPPER_PORT_LAST; port++) {
		if (rtl838x_phy_read_c22(priv, port, 1, &bmsr) ||
		    rtl838x_phy_read_c22(priv, port, 1, &bmsr))
			continue;
		if (!rtl838x_phy_read_c22(priv, port, 2, &phy_id1) &&
		    !rtl838x_phy_read_c22(priv, port, 3, &phy_id2))
			printf("rtl8380: PHY %d ID %04x:%04x BMSR %04x\n",
			       port, phy_id1, phy_id2, bmsr);
	}
	printf("rtl8380: TX mask %08x (all copper ports, unconditionally)\n",
	       priv->tx_port_mask);
	return 0;
}

static void rtl838x_eth_decode_speed(struct rtl838x_priv *priv, u32 mac_links,
				      struct rtl838x_link_snapshot *snapshot)
{
	u32 speed_status = rtl_readl(priv, RTL838X_MAC_LINK_SPD_STS);
	int port;

	snapshot->gigabit = mac_links & RTL838X_SFP_PORT_MASK;
	for (port = RTL838X_COPPER_PORT_FIRST;
	     port <= RTL838X_COPPER_PORT_LAST; port++) {
		u32 speed;

		if (!(mac_links & BIT(port)))
			continue;
		speed = (speed_status >> (2 * port)) & 0x3;
		if (speed == RTL838X_MAC_SPEED_1000)
			snapshot->gigabit |= BIT(port);
		else if (speed == 3)
			snapshot->unknown_speed |= BIT(port);
	}
}

static u32 rtl838x_eth_phy_links(struct rtl838x_priv *priv)
{
	u32 phy_links = 0;
	u16 bmsr;
	int port;

	for (port = RTL838X_COPPER_PORT_FIRST;
	     port <= RTL838X_COPPER_PORT_LAST; port++) {
		/* BMSR link is latch-low; the second read is current status. */
		if (!rtl838x_phy_read_c22(priv, port, 1, &bmsr) &&
		    !rtl838x_phy_read_c22(priv, port, 1, &bmsr) &&
		    (bmsr & RTL838X_PHY_BMSR_LINK_STATUS))
			phy_links |= BIT(port);
	}
	return phy_links;
}

int rtl838x_eth_link_snapshot(struct udevice *dev,
			      struct rtl838x_link_snapshot *snapshot)
{
	struct rtl838x_priv *priv;
	u32 phy_links, mac_links = 0;

	if (!dev || !snapshot)
		return -EINVAL;
	priv = dev_get_priv(dev);
	/*
	 * enable_copper_ports() no longer waits before returning (it floods
	 * every copper port instead of picking one, so it no longer needs to).
	 * This function is the one place that still needs a link snapshot to
	 * be complete rather than instant: sg1002_led_link_status() calls it
	 * exactly once, at boot, and nothing else does -- unlike
	 * enable_copper_ports(), a wait here is never repeated on every
	 * ping/tftpboot/dhcp.
	 *
	 * Always spend the full 50x100ms (5 s), never less: an earlier version
	 * of this loop exited as soon as whichever ports had shown up in
	 * phy_links so far agreed with mac_links, which is the same mistake as
	 * F04/F25 in doc/board/intelbras/sg1002-mr-findings.md in a third place
	 * -- a port whose partner is slower (measured on this board: one
	 * copper port reached MAC_LINK_STS well after another had already made
	 * phy_links and mac_links agree) never gets a chance to be seen if the
	 * loop leaves as soon as the faster one looks consistent. 5 s matches
	 * the window enable_copper_ports() itself used and was tested against,
	 * before that wait was removed there. This only feeds the
	 * informational "LED link: lanX" boot line and the LEDs' initial
	 * colour, never tftpboot/ping/dhcp, and a link still not up after 5 s
	 * is corrected within ~200 ms anyway by the cyclic LED poll in
	 * board/intelbras/sg1002_mr/sg1002_mr.c, so the fixed wait costs
	 * nothing but boot time, once.
	 */
	mdelay(5000);
	phy_links = rtl838x_eth_phy_links(priv);
	(void)rtl_readl(priv, RTL838X_MAC_LINK_STS);
	mac_links = rtl_readl(priv, RTL838X_MAC_LINK_STS);
	snapshot->links = (phy_links | mac_links) &
		(RTL838X_COPPER_PORT_MASK | RTL838X_SFP_PORT_MASK);
	snapshot->unknown_speed = phy_links & ~mac_links;
	rtl838x_eth_decode_speed(priv, mac_links, snapshot);
	return 0;
}

int rtl838x_eth_link_snapshot_now(struct udevice *dev,
				  struct rtl838x_link_snapshot *snapshot)
{
	struct rtl838x_priv *priv;
	u32 mac_links;

	if (!dev || !snapshot)
		return -EINVAL;
	priv = dev_get_priv(dev);
	(void)rtl_readl(priv, RTL838X_MAC_LINK_STS);
	mac_links = rtl_readl(priv, RTL838X_MAC_LINK_STS);
	snapshot->links = mac_links &
		(RTL838X_COPPER_PORT_MASK | RTL838X_SFP_PORT_MASK);
	snapshot->unknown_speed = 0;
	rtl838x_eth_decode_speed(priv, mac_links, snapshot);
	return 0;
}

int rtl838x_eth_port_octets(struct udevice *dev, unsigned int port,
			    u32 *rx, u32 *tx)
{
	struct rtl838x_priv *priv;
	u32 port_base;

	if (!dev || !rx || !tx || port >= RTL838X_CPU_PORT)
		return -EINVAL;
	priv = dev_get_priv(dev);
	/*
	 * OpenWrt Linux GPL drivers/net/dsa/rtl83xx/{dsa,rtl838x}.c:
	 * standard MIB base 0x1200, 0x100 bytes per port, 64-bit inbound
	 * octets at offset 0xf8 and outbound at 0xf0. Low words suffice to
	 * detect a change, including a normal 32-bit wrap.
	 */
	port_base = RTL838X_STAT_PORT_STD_MIB + (port + 1) * 0x100 - 4;
	*rx = rtl_readl(priv, port_base - 0xf8);
	*tx = rtl_readl(priv, port_base - 0xf0);
	return 0;
}

static void rtl838x_setup_rings(struct rtl838x_priv *priv)
{
	unsigned int i;

	priv->rx_ring = rtl_uncached(priv->rx_ring_mem);
	priv->rx_frag = rtl_uncached((uchar *)priv->rx_ring_mem + RTL838X_RX_COUNT * sizeof(u32));
	priv->tx_ring = rtl_uncached(priv->tx_ring_mem);
	priv->tx_frag = rtl_uncached((uchar *)priv->tx_ring_mem + sizeof(u32));
	priv->rx_buf = rtl_uncached(priv->rx_buf_mem);
	priv->tx_buf = rtl_uncached(priv->tx_buf_mem);
	/* Never dirty KSEG0 cache lines for DMA memory later used via KSEG1. */
	memset(priv->rx_ring, 0, RTL838X_RX_COUNT * sizeof(u32) +
	       RTL838X_RX_COUNT * sizeof(struct rtl838x_frag));
	memset(priv->tx_ring, 0, sizeof(u32) + sizeof(struct rtl838x_frag));
	memset(priv->rx_buf, 0, RTL838X_RX_COUNT * RTL838X_PKT_LEN);
	memset(priv->tx_buf, 0, RTL838X_PKT_LEN);
	priv->rx_slot = 0;
	for (i = 0; i < RTL838X_RX_COUNT; i++) {
		priv->rx_frag[i].dma = virt_to_phys(priv->rx_buf + i * RTL838X_PKT_LEN);
		priv->rx_frag[i].size = RTL838X_PKT_LEN;
		priv->rx_ring[i] = virt_to_phys(&priv->rx_frag[i]) | RTL838X_RING_OWN;
	}
	priv->rx_ring[RTL838X_RX_COUNT - 1] |= RTL838X_RING_WRAP;
	priv->tx_ring[0] = virt_to_phys(priv->tx_frag) | RTL838X_RING_WRAP;
}

static int rtl838x_eth_start(struct udevice *dev)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	u32 value;
	unsigned int i;

	value = rtl_readl(priv, RTL838X_CPU_L2_PORT_CTRL);
	rtl_writel(priv, value & ~0x3, RTL838X_CPU_L2_PORT_CTRL);
	mdelay(100);
	rtl_writel(priv, rtl_readl(priv, RTL838X_RST_GLB_CTRL) | 0xc, RTL838X_RST_GLB_CTRL);
	for (i = 0; i < 1000; i++) {
		if (!(rtl_readl(priv, RTL838X_RST_GLB_CTRL) & 0xc))
			break;
		udelay(100);
	}
	if (i == 1000)
		return -ETIMEDOUT;
	/* Let the switch core finish its reset before programming DMA state. */
	mdelay(100);
	/*
	 * Zero selects free-running descriptor rings.  Leaving reset-era values
	 * here constrains queue 0 and can stop RX although the PHY link is up.
	 */
	rtl_writel(priv, 0, RTL838X_DMA_RX_RING_SIZE);
	if (rtl838x_enable_copper_ports(dev))
		return -EIO;
	rtl838x_setup_rings(priv);
	rtl_writel(priv, 0, RTL838X_QM_PKT2CPU_MAP);
	for (i = 0; i < 3; i++)
		rtl_writel(priv, 0, RTL838X_QM_RSN2CPUQ + i * 4);
	rtl_writel(priv, virt_to_phys(priv->rx_ring), RTL838X_DMA_RX_BASE);
	rtl_writel(priv, virt_to_phys(priv->tx_ring), RTL838X_DMA_TX_BASE);
	rtl_writel(priv, 0, RTL838X_DMA_INTR_MSK);
	rtl_writel(priv, ~0U, RTL838X_DMA_INTR_STS);
	rtl_writel(priv, (rtl_readl(priv, RTL838X_RX_FILTER) & ~0x3fff) | RTL838X_PKT_LEN,
		    RTL838X_RX_FILTER);
	rtl_writel(priv, (rtl_readl(priv, RTL838X_TX_FILTER) & ~0x3fff) | RTL838X_PKT_LEN,
		    RTL838X_TX_FILTER);
	rtl_writel(priv, RTL838X_DMA_TX_PAD | RTL838X_DMA_ENABLE, RTL838X_DMA_CTRL);
	rtl_writel(priv, 0x6192f, RTL838X_CPU_FORCE_MODE);
	rtl_writel(priv, rtl_readl(priv, RTL838X_CPU_L2_PORT_CTRL) | 0xb,
		    RTL838X_CPU_L2_PORT_CTRL);
	return 0;
}

static void rtl838x_eth_stop(struct udevice *dev)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	rtl_writel(priv, rtl_readl(priv, RTL838X_DMA_CTRL) & ~RTL838X_DMA_ENABLE, RTL838X_DMA_CTRL);
}

static int rtl838x_eth_write_hwaddr(struct udevice *dev)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	struct eth_pdata *pdata = dev_get_plat(dev);
	const uchar *mac = pdata->enetaddr;
	u32 hi = ((u32)mac[0] << 8) | mac[1];
	u32 lo = ((u32)mac[2] << 24) | ((u32)mac[3] << 16) |
		 ((u32)mac[4] << 8) | mac[5];

	/* The ASIC keeps the CPU MAC in three lookup/filter blocks. */
	rtl_writel(priv, hi, RTL838X_MAC_ADDR_CTRL);
	rtl_writel(priv, lo, RTL838X_MAC_ADDR_CTRL + 4);
	rtl_writel(priv, hi, RTL838X_MAC_ADDR_CTRL_ALE);
	rtl_writel(priv, lo, RTL838X_MAC_ADDR_CTRL_ALE + 4);
	rtl_writel(priv, hi, RTL838X_MAC_ADDR_CTRL_MAC);
	rtl_writel(priv, lo, RTL838X_MAC_ADDR_CTRL_MAC + 4);
	return 0;
}

static int rtl838x_eth_send(struct udevice *dev, void *packet, int length)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	unsigned int timeout;
	int tx_length;

	if (length <= 0 || length > RTL838X_PKT_LEN - 4)
		return -EINVAL;
	if (priv->tx_ring[0] & RTL838X_RING_OWN)
		return -EBUSY;
	memcpy(priv->tx_buf, packet, length);
	/* The RTL8380 engine expects an empty four-byte FCS slot in its buffer. */
	tx_length = length + 4;
	if (tx_length < 64)
		tx_length = 64;
	memset(priv->tx_buf + length, 0, tx_length - length);
	memset(priv->tx_frag->cpu_tag, 0, sizeof(priv->tx_frag->cpu_tag));
	/*
	 * RTL8380 direct-port-mask CPU tag.  Prefer only the currently linked
	 * copper port(s): this avoids depending on the ASIC's treatment of a
	 * multi-port direct mask during early switch initialization.  If link state
	 * has not settled yet, start() retains the all-copper fallback.
	 */
	priv->tx_frag->cpu_tag[1] = 0x0400;
	priv->tx_frag->cpu_tag[2] = 0x0200;
	priv->tx_frag->cpu_tag[4] = priv->tx_port_mask >> 16;
	priv->tx_frag->cpu_tag[5] = priv->tx_port_mask & 0xffff;
	priv->tx_frag->len = tx_length;
	priv->tx_frag->dma = virt_to_phys(priv->tx_buf);
	priv->tx_ring[0] = virt_to_phys(priv->tx_frag) | RTL838X_RING_WRAP | RTL838X_RING_OWN;
	rtl_writel(priv, rtl_readl(priv, RTL838X_DMA_CTRL) | RTL838X_DMA_TRIGGER_TX,
		    RTL838X_DMA_CTRL);
	for (timeout = 0; timeout < 10000; timeout++) {
		if (!(priv->tx_ring[0] & RTL838X_RING_OWN))
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

static int rtl838x_eth_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	unsigned int slot = priv->rx_slot;
	u16 length;

	if (priv->rx_ring[slot] & RTL838X_RING_OWN)
		return -EAGAIN;
	length = priv->rx_frag[slot].len;
	if (length < 4 || length > RTL838X_PKT_LEN) {
		/* No packet reaches free_pkt() on an error: return this slot now. */
		priv->rx_frag[slot].len = 0;
		priv->rx_ring[slot] = virt_to_phys(&priv->rx_frag[slot]) | RTL838X_RING_OWN |
			(slot == RTL838X_RX_COUNT - 1 ? RTL838X_RING_WRAP : 0);
		priv->rx_slot = (slot + 1) % RTL838X_RX_COUNT;
		return -EIO;
	}
	*packetp = priv->rx_buf + slot * RTL838X_PKT_LEN;
	return length - 4;
}

static int rtl838x_eth_free_pkt(struct udevice *dev, uchar *packet, int length)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	unsigned int slot = priv->rx_slot;

	priv->rx_frag[slot].len = 0;
	priv->rx_ring[slot] = virt_to_phys(&priv->rx_frag[slot]) | RTL838X_RING_OWN |
		(slot == RTL838X_RX_COUNT - 1 ? RTL838X_RING_WRAP : 0);
	priv->rx_slot = (slot + 1) % RTL838X_RX_COUNT;
	return 0;
}

static int rtl838x_eth_probe(struct udevice *dev)
{
	struct rtl838x_priv *priv = dev_get_priv(dev);
	size_t rx_ring_size = ALIGN(RTL838X_RX_COUNT *
		(sizeof(u32) + sizeof(struct rtl838x_frag)), ARCH_DMA_MINALIGN);
	size_t tx_ring_size = ALIGN(sizeof(u32) + sizeof(struct rtl838x_frag),
		ARCH_DMA_MINALIGN);
	size_t rx_buf_size = RTL838X_RX_COUNT * RTL838X_PKT_LEN;

	priv->base = dev_remap_addr(dev);
	if (!priv->base)
		return -EINVAL;
	priv->rx_ring_mem = memalign(ARCH_DMA_MINALIGN, rx_ring_size);
	priv->tx_ring_mem = memalign(ARCH_DMA_MINALIGN, tx_ring_size);
	priv->rx_buf_mem = memalign(ARCH_DMA_MINALIGN, rx_buf_size);
	priv->tx_buf_mem = memalign(ARCH_DMA_MINALIGN, RTL838X_PKT_LEN);
	if (!priv->rx_ring_mem || !priv->tx_ring_mem || !priv->rx_buf_mem ||
	    !priv->tx_buf_mem) {
		free(priv->tx_buf_mem);
		free(priv->rx_buf_mem);
		free(priv->tx_ring_mem);
		free(priv->rx_ring_mem);
		return -ENOMEM;
	}
	/*
	 * Evict dirty KSEG0 lines left by the heap's previous users before the
	 * uncached aliases in setup_rings() initialize the same physical memory.
	 * Keep each allocation cache-line sized so heap metadata cannot later
	 * dirty a line shared with a DMA descriptor.
	 */
	flush_dcache_range((ulong)priv->rx_ring_mem,
		(ulong)priv->rx_ring_mem + rx_ring_size);
	flush_dcache_range((ulong)priv->tx_ring_mem,
		(ulong)priv->tx_ring_mem + tx_ring_size);
	flush_dcache_range((ulong)priv->rx_buf_mem,
		(ulong)priv->rx_buf_mem + rx_buf_size);
	flush_dcache_range((ulong)priv->tx_buf_mem,
		(ulong)priv->tx_buf_mem + RTL838X_PKT_LEN);
	return 0;
}

static const struct eth_ops rtl838x_eth_ops = {
	.start = rtl838x_eth_start,
	.stop = rtl838x_eth_stop,
	.send = rtl838x_eth_send,
	.recv = rtl838x_eth_recv,
	.free_pkt = rtl838x_eth_free_pkt,
	.write_hwaddr = rtl838x_eth_write_hwaddr,
};

static const struct udevice_id rtl838x_eth_ids[] = {
	{ .compatible = "realtek,rtl8380-eth" },
	{ }
};

U_BOOT_DRIVER(rtl838x_eth) = {
	.name = "rtl838x_eth",
	.id = UCLASS_ETH,
	.of_match = rtl838x_eth_ids,
	.probe = rtl838x_eth_probe,
	.ops = &rtl838x_eth_ops,
	.priv_auto = sizeof(struct rtl838x_priv),
	.plat_auto = sizeof(struct eth_pdata),
};

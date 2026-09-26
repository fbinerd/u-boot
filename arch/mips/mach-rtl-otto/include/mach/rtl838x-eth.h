/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MACH_RTL838X_ETH_H
#define __MACH_RTL838X_ETH_H

#include <linux/types.h>

struct udevice;

/* Bit positions are the switch MAC port numbers, not front-panel numbers. */
struct rtl838x_link_snapshot {
	u32 links;
	u32 gigabit;
	u32 unknown_speed;
};

int rtl838x_eth_link_snapshot(struct udevice *dev,
			      struct rtl838x_link_snapshot *snapshot);
int rtl838x_eth_link_snapshot_now(struct udevice *dev,
				  struct rtl838x_link_snapshot *snapshot);
int rtl838x_eth_port_octets(struct udevice *dev, unsigned int port,
			    u32 *rx, u32 *tx);

#endif

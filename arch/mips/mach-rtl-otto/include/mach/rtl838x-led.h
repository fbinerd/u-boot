/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RTL838x port LED engine, software control.
 *
 * Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>
 *
 * Register interface derived from the GPL Linux gpio-rtl8380-portled
 * driver of this project's OpenWrt tree, and measured on this hardware.
 */
#ifndef __MACH_RTL838X_LED_H
#define __MACH_RTL838X_LED_H

#include <linux/types.h>

#define RTL838X_LED_PORTS	28
#define RTL838X_LED_CHANNELS	2	/* channel 0 and 1 of every port */

/*
 * Take the LED engine out of its automatic (link/activity) mode: enable the
 * LEDs of the ports in @port_mask, put the two channels of every port under
 * software control and switch them all off.
 */
void rtl838x_port_led_init(u32 port_mask);

/* Steadily light (@on) or switch off channel @channel of LED port @port. */
void rtl838x_port_led_set(unsigned int port, unsigned int channel, bool on);

#endif

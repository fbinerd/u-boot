/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Realtek RTL8231 GPIO expander, reached over the RTL838x auxiliary MDIO
 * controller (SoC GPIO2/GPIO3 muxed as MDC/MDIO).
 *
 * Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>
 *
 * Register interface derived from the GPL Linux drivers
 * (mdio-realtek-otto-aux, rtl8231 MFD and pinctrl) and the RTL8231
 * datasheet.
 */
#ifndef __MACH_RTL8231_H
#define __MACH_RTL8231_H

#define RTL8231_NGPIO	37

/*
 * Bring up the chip at aux-MDIO address @addr: enable the SoC side of the
 * bus, check the ready code and, if the chip has not been started yet (cold
 * boot), soft-reset it into an all-inputs GPIO state and start it.  An
 * expander that is already running (warm reset, Linux ran before) is left
 * as it is.  Returns 0, -ETIMEDOUT (bus stuck) or -ENODEV (no RTL8231).
 */
int rtl8231_init(unsigned int addr);

/*
 * Drive GPIO @pin (0..36) of the expander at @addr as an output at
 * @value (electrical level, no polarity handling).  The pin is muxed to
 * GPIO, its level is written first and only then is it made an output, so
 * it never glitches.  Other pins are not touched.
 */
int rtl8231_gpio_set_output(unsigned int addr, unsigned int pin, int value);

#endif

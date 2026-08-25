/* SPDX-License-Identifier: GPL-2.0+ */
/* Configuration for the Amlogic GXLX P271 board. */

#ifndef __P271_CONFIG_H
#define __P271_CONFIG_H

/* Execute an optional ROM-USB script only after validating its image magic. */
#define BOOTENV_DEV_ROMUSB(devtypeu, devtypel, instance) \
	"bootcmd_romusb=" \
		"if test \"${boot_source}\" = \"usb\" && " \
				"test -n \"${scriptaddr}\"; then " \
			"if itest.l *${scriptaddr} == 0x56190527 || " \
				"itest.l *${scriptaddr} == 0xedfe0dd0; then " \
				"echo '(ROM USB boot)'; " \
				"source ${scriptaddr}; " \
			"fi; " \
		"fi\0"

/*
 * The factory eMMC uses the legacy Amlogic partition format. Keep all MMC
 * commands available, but do not probe unsupported partitions during autoboot.
 */
#define BOOT_TARGET_DEVICES(func) \
	func(ROMUSB, romusb, na) \
	func(USB_DFU, usbdfu, na) \
	BOOT_TARGET_PXE(func) \
	BOOT_TARGET_DHCP(func)

#include <configs/meson64.h>

#endif /* __P271_CONFIG_H */

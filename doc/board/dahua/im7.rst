.. SPDX-License-Identifier: GPL-2.0+

Fullhan FH8626 Board (Intelbras Mibo iM7 / Dahua Imou IPC-S21F)
===============================================================

This document describes how to build and run U-Boot on the Fullhan FH8626 SoC
found in the Intelbras Mibo iM7 and Dahua Imou IPC-S21F IP cameras (and related
family devices like IPC-A22E, IPC-C22E, IPC-F22, IPC-TA22CP).

Hardware Overview
-----------------

* **SoC**: Fullhan FH8626V100 / FH8626 (ARM Cortex-A7 @ 1.0 GHz, ARMv7-A)
* **RAM**: 64 MB DDR (Base: ``0xA0000000``)
* **Flash**: 8 MB SPI NOR Flash (Eon EN25QH64)
* **Serial**: UART0 at ``0xF0700000`` (115200 bps, 8N1)
* **Ethernet**: 10/100 Mbps GMAC
* **Storage**: SD/MMC controller at ``0xF0400000``

Flash Partition Layout
----------------------

* ``0x000000 - 0x050000`` (320 KB): U-Boot bootloader
* ``0x050000 - 0x060000`` (64 KB): Environment / HWID
* ``0x060000 - 0x070000`` (64 KB): Partition table (CramFS)
* ``0x070000 - 0x1D0000`` (1408 KB): Linux Kernel (uImage Linux-4.9)
* ``0x1D0000 - 0x760000`` (5696 KB): Root Filesystem (SquashFS)
* ``0x760000 - 0x7B0000`` (320 KB): Active Configuration (JFFS2)
* ``0x7B0000 - 0x800000`` (320 KB): Backup Configuration (JFFS2)

Building U-Boot
---------------

To configure and compile U-Boot for this device:

.. code-block:: bash

    export ARCH=arm
    export CROSS_COMPILE=arm-linux-gnueabi-
    make fullhan_im7_defconfig
    make -j$(nproc)

Packaging for SPI Flash (320 KB)
--------------------------------

The compiled ``u-boot.bin`` can be combined with the Fullhan 2BL/W975 preloader
header to generate the exact 320 KB flash image:

.. code-block:: bash

    python3 tools/pack_uboot.py orig_0_U-Boot.bin u-boot.bin u-boot-im7-320k.bin

Features
--------

* Unlocked TTL debug console (115200 8N1)
* Fast autoboot of OEM / OpenWrt / Linux kernels
* 100% GPL-2.0+ Open Source clean-room implementation

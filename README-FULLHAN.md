# U-Boot for Fullhan FH8626 / FH8852 / FH8856 SoCs

Open-source U-Boot board support package (BSP) for **Fullhan Microelectronics FH8626** ARM Cortex-A7 processor, implemented for **Intelbras Mibo iM7 / Dahua Imou IPC-S21F** and compatible IP cameras.

## 🎯 Target Hardware Specifications
- **SoC:** Fullhan FH8626 / FH8626V100 (ARM Cortex-A7 @ 1.0 GHz, ARMv7-A)
- **RAM:** 64 MB DDR (Base: `0xA0000000`)
- **Storage:** 8 MB SPI NOR Flash (`EN25QH64`) / SD Card
- **UART:** UART0 (`0xF0700000`), 115200 8N1
- **Supported Devices:**
  - Intelbras Mibo iM7 (`iM7-FC`)
  - Dahua Imou IPC-S21F / IPC-S21F-V2
  - Dahua Imou IPC-A22E / IPC-C22E / IPC-F22 / IPC-TA22CP

## 🛠️ How to Build
```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
make fullhan_im7_defconfig
make -j$(nproc)
```

## 📜 License
100% Free and Open Source under **GPL-2.0+**.

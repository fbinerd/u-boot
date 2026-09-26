# SG1002 MR QEMU model: findings and mutation coverage

Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>

SPDX-License-Identifier: GPL-2.0+

A behavioural QEMU 9.1 machine model of this board (`rtl8380-sg1002-mr`),
kept in a separate repository, boots the flash image from the reset vector
(Stage-1, Stage-2, then a stub kernel or the vendor bootrom) with a mutable
SPI NOR, a real watchdog, the LED engine, and the RTL8231, modelled from the
vendor's GPL SDK, the Linux drivers, and register-level measurements on the
physical board.

It tests **logic and control flow**, not the board itself: DRAM is
perfect (training always passes), there is no cache model, no timing
hazards, and no Ethernet PHY. A green run in the emulator is not proof for
Stage-1's DDR margin or for anything Ethernet-related; a red run means
investigate the code, the model, and the scenario, in that order.

This note keeps the model's own findings and mutation-testing coverage
separate from what only the physical board can answer. `emu_test.py`
defaults to a fixed board-verified reference image (`SG1002_BOOT_REGION`
picks another one); see the emulator's own README for how to run it.

## Findings

| ID | Severity | Class | Evidence | Changes the flashed binary? | Board |
|---|---|---|---|---|---|
| E1 | medium | [BOARD] | The Linux 6.18.44 driver's comment claims the watchdog cannot be stopped after phase 1, but on this physical RTL8380M, `INTR` read `0x80000000` before disarming it, `CTRL` read `0x000f8000` afterwards, and no reset followed in the next 8 s. The original model already reproduced this. | no | Confirmed on this board; another Otto variant may differ. |
| E2 | medium | [EMU] | The vendor GPL SDK (`nor_spi_flash.c:60-70`) waits for READY and accounts for an unresponsive controller. A `spi-ready-when-cs0` fault injection makes the `sf-ready-timeout-releases-cs` scenario confirm the fix deasserts CS0 on a timeout; a mutation that removed that cleanup failed the scenario as expected. | no | The READY timeout itself was not induced on the board. |
| E3 | medium | [EMU] | `uart-thre-retries` holds THRE low for 64 LSR reads after a command and confirms output still recovers; a mutation that kept THRE from ever returning failed the scenario. This covers the driver retrying a serial write, not the physical lockup with a high baud rate, nor Stage-1's final wait. | no | Non-default `baudrate` on real hardware is still an open item. |
| E4 | low | [EMU] | With the second pinmux bit's requirement temporarily removed, `aux-mdio-needs-pinmux` passed; asserting for the single-bit state made the same mutation fail. The model was restored. | no | Both bits being required was already measured on the board; no new flash write. |
| E5 | low | [EMU] | A temporary mutation suppressing the watchdog's second phase failed `wdt-reset` (no second banner within 30 s); restored, the scenario passes at 336 ms. | no | On the physical board, `reset` to banner took 7.748 s and to the prompt 13.562 s -- the whole boot, not an isolated measurement of the modelled 336 ms. |
| E6 | medium | [STATIC] | The model and the GPL SDK's `nor_spi_flash.c:41-45` agree on CS0, READY, and length; the model's page program uses bit-AND semantics. This backs the logic `sf-semantics` exercises, without proving real flash timing. | no | RDID, SFDP, and SR1 measured on the board; program/erase timing remains unmeasured. |
| E7 | low | [BOARD] | `sf probe 0:0` reported `w25q128`, a 256-byte page, a 4 KiB sector, and 16 MiB; the Linux sysfs reported `jedec_id=ef4018` and SFDP `53 46 44 50 05 01 00 ff`. A direct SPI `RDSR` (`0x05`) transaction through the controller read SR1 as `0x00` both before and after `sf probe`, with CS0 deasserted at the end of each read. | no | No block-protect bits were active to clear in this test; it does not demonstrate clearing a previously-active protection. |
| E8 | high | [STATIC] | Stage-1 programs `Config.K0=3` (cacheable non-coherent, per `asm/mipsregs.h`). Before the fix (see E10), `rtl838x_setup_rings()` zeroed the DMA allocations through KSEG0 and then used them through KSEG1; dirty lines could overwrite the descriptors. | yes, Stage-2 | The physical failure and its fix are recorded under E10. |
| E9 | medium | [EMU] | The Linux DTS (`rtl838x.dtsi`) declares the UART with `reg-io-width = <1>` and `reg-shift = <2>`; the 8250 driver accesses the first byte of each big-endian word. The QEMU model used to treat the *last* byte as the register. `uart-byte-lane` requires 8-bit and 32-bit reads to agree and an 8-bit SCR write to be visible through a 32-bit read; the old implementation failed it, and the fixed model passed against the physical V219 reference. | no | No firmware change; measuring the UART with a non-default divisor on hardware is still open. |
| E10 | high | [BOARD] | With two links present, the first large TFTP transfer after a reset repeated `ACK 10` for `DATA 11` (confirmed with a `tcpdump` UDP capture). `md.l` showed valid descriptors in the RX ring through the KSEG1 alias and stale environment bytes at the same physical address through KSEG0; after the failure, the KSEG1 alias showed the same stale bytes too. Fixed by flushing and invalidating all four DMA allocations before using them through KSEG1. On the board afterwards, both aliases matched, three first transfers after reset (with both links) passed, a Stage-2 reread matched all 224 KiB, and OpenWrt booted. | yes, Stage-2 | Validated on this board; margin on other boards/conditions is untested. |

The model defaults to a Winbond JEDEC ID with a Macronix alternative for
the vendor bootrom. The board read confirmed the JEDEC ID, the SFDP header,
and SR1=`0x00` before/after `sf probe`. QEMU's DRAM is perfect, there is no
cache model, no Ethernet PHY, no real Linux, and no controller timing
contention. A green suite only means no regression in the logic paths the
model actually represents.

## Mutations and coverage

- Detected: suppressing the watchdog's second phase turned `wdt-reset` red.
- Not detected by the older test: ignoring the second pinmux bit left the
  scenario green. New assertions now cover each bit in isolation; a
  mutation ignoring either bit now fails.
- New coverage: `wdt-phase1-can-stop` requires phase 1 to be interruptible,
  `ENABLE` cleared, and no reset past the phase-2 deadline. The original
  model passed, and the behaviour was also measured on the board (E1). A
  mutation that skipped disarming after phase 1 failed the scenario.
- New coverage: `sf-ready-timeout-releases-cs` forces READY low while CS0
  is asserted; a mutation removing the CS0 cleanup on error failed it.
- New coverage: `uart-thre-retries` holds THRE for 64 reads after a
  command, confirms output recovers, and fails if the bit never returns.
  Stage-1's final wait and the physical lockup with a different baud rate
  both remain unmodelled.
- New coverage: `uart-byte-lane` checks the byte position on UART reads and
  writes. Temporarily restoring the old last-byte selection failed the
  scenario; the fixed model passes.

Every mutation was reverted afterwards. The new pinmux test only changes
`emu_test.py`, never Stage-1, Stage-2, or the board's flash. The full suite
passes 23 of 23 scenarios against the board-verified reference; the
`uart-byte-lane` scenario also passes against the older V219 reference.

## Outstanding physical measurements

1. Isolate the watchdog's own latency within the 7.748 s measured from
   `reset` to the banner. The silent Stage-1 gives no intermediate marker;
   the model's virtual 336 ms has not been confirmed on the board.
2. Measure `sf probe` with a block-protect bit genuinely active
   beforehand, if a safe way to set that state up is found. SR1 was
   already `0x00` before probing in this round.
3. After any further Stage-1 change, repeat resets and an OpenWrt boot;
   evaluate a cold start, temperature, and another board separately.
4. Confirm the RTL8231's genuine cold start, and the hardware-dependent
   error paths for DDR, UART, and the SPI controller.

DDR training margin and UART/SPI timing outside the cases already measured
cannot be verified this way: the virtual devices available do not
represent those effects. The E10 cache-coherency defect was measured
directly on the board.

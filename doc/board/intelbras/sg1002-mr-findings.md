# SG1002 MR boot chain: findings and status

Copyright (C) 2026 Fabiano Tassotti <fabianotassotti@gmail.com>

SPDX-License-Identifier: GPL-2.0+

This is the single reference for **what is fixed and verified, what is a
known and accepted limitation, and what is genuinely not reviewed**, for
anyone picking this tree up to review or extend it. See `sg1002-mr.rst` in
this directory for what the board and the boot chain are; this document
only tracks findings and their evidence.

Evidence classes used below: **[BOARD]** observed on the physical unit,
**[EMU]** run in the QEMU behavioural model (see `sg1002-mr-emulator.md`),
**[STATIC]** comparison against source, the vendor GPL SDK, a datasheet, or
disassembly. A green result in the emulator is not proof for anything the
emulator does not model (DDR training margin, cache, real timing,
Ethernet) -- see `sg1002-mr-emulator.md` for its own scope.

## Scope and how to review

`git diff <the "Prepare v2026.07" upstream commit this tree branched
from>..HEAD` is the whole diff: roughly 4,000 lines of code, the rest is
this documentation. Slices, in rough order of risk:

| Slice | Files | Lines |
|---|---|---|
| Stage-1 (DDR-training preloader) | `arch/mips/mach-rtl-otto/preloader/` | ~1,600 |
| Drivers | `drivers/net/rtl838x_eth.c`, `drivers/spi/rtl8380_spi.c`, `drivers/serial/serial_rtl8380.c`, `arch/mips/mach-rtl-otto/{rtl8231,rtl838x_led}.c` | ~1,300 |
| Changes to upstream files | `common/board_f.c`, `common/board_r.c`, `lib/fdtdec.c`, `drivers/serial/serial-uclass.c`, `arch/mips/lib/{reloc,bootm}.c`, `arch/mips/mach-rtl-otto/cpu.c`, `boot/bootm.c`, and the Kconfig/Makefile plumbing | ~400 of diff |
| Board and build | `board/intelbras/sg1002_mr/`, `tools/build-sg1002-mr.sh`, `tools/check-sg1002-mr-*.{sh,py}` | ~450 |

## The reproducibility invariant

`tools/check-sg1002-mr-repro.py` compares a fresh build against a
board-verified reference image, allowing a difference only inside the
embedded U-Boot version string. This is what makes every entry below
checkable: **if the binary stays identical, a change is a pure cleanup and
needs no new hardware test; if it changes, it needs one.** The current
board-verified reference is `artifacts/known-good-v219/` (see
`sg1002-mr.rst`); every later change described below was written straight
to the running board over TFTP + `sf`, always with a full readback
comparison, and is only preserved here as a finding, not as a git-tracked
binary (the intermediate `known-good-vNNN` checkpoints from day-to-day
development were pruned; the commit history was reorganized by
implementation area, so version numbers below are historical labels for
individual physical tests, not tags in this tree).

## Findings

| id | file | finding | severity | changes the flashed binary? | evidence |
|---|---|---|---|---|---|
| F01 | `arch/mips/mach-rtl-otto/cpu.c` | Reads CP0 cache geometry before invalidating it. | high, fixed | yes, Stage-2 | [BOARD] [STATIC]: full boot-region readback, a power cycle, and OpenWrt boot. |
| F02 | `boot/bootm.c` | Flushes the real physical region after remapping the kernel's load destination, so a cacheable alias cannot serve stale data. | high, fixed | yes, Stage-2 | [BOARD] [EMU]: `bootm` KSEG0/KSEG1 scenarios. |
| F03 | `drivers/net/rtl838x_eth.c` | Recycles a malformed RX descriptor instead of leaving the ring stuck. | medium, fixed | yes, Stage-2 | [BOARD] [STATIC]: physical boot; the malformed case itself was not induced on the board. |
| F04 | `drivers/net/rtl838x_eth.c` | `rtl838x_eth_start()` used to wait up to a fixed 5 s for both copper links before choosing a single TX port, to avoid picking a faster, unrelated port before the actual host's port had negotiated (two-cable TFTP). | high, **superseded by F26/F27**, see below | — | [BOARD]: 20 resets, ping and TFTP with two cables. |
| F05 | `board/intelbras/sg1002_mr/sg1002_mr.c` | Rejects a boot address outside the SPI XIP window and clears a stale saved `bootcmd` when the saved address is invalid, instead of silently booting the old target. | high, fixed | yes, Stage-2 | [BOARD] [EMU]: invalid environment in the emulator, 20 physical resets. |
| F06 | `arch/mips/mach-rtl-otto/preloader/sg1002_mr.lds` | Reserves SRAM for the calibration stack below the copied payload. | high, fixed | yes, Stage-1 | [BOARD] [STATIC]: `__sram_payload_end` verified below the stack reserve by the linker; 20 resets and OpenWrt. |
| F07 | `arch/mips/mach-rtl-otto/preloader/sg1002_mr_sram.S` | Stage-1 now stops if the DQ scan or the PHY commit fails, and bounds the final UART busy-wait, instead of continuing on a failed DDR calibration. | high, fixed | yes, Stage-1 | [BOARD] [EMU]: 20 resets. A diagnostic build that forces a real calibration failure printed the `E` halt marker on the board with no banner, as intended (see "Physical DDR failure test" below); the known-good Stage-1 was restored via an external CH341A programmer and the board booted normally afterwards. |
| F08 | `drivers/spi/rtl8380_spi.c` | Deasserts CS0 on every transfer-error return path, not just on success. | medium, fixed | yes, Stage-2 | [BOARD] [EMU]: `sf probe`/`sf read`; a forced READY timeout in the emulator. |
| F09 | `lib/fdtdec.c` | Re-validates the control DTB's header after relocation; this board's own early handoff used to skip that check entirely. | high, fixed | yes, Stage-2 | [BOARD] [EMU]: an invalid header is rejected before the banner in the emulator; physical reread and OpenWrt boot. |
| F10 | `board/intelbras/sg1002_mr/sg1002_mr.c` | Watchdog MMIO now goes through the uncached KSEG1 alias, matching Stage-1. | medium, fixed | yes, Stage-2 | [BOARD] [EMU]: watchdog reset in the emulator, physical reread. |
| F11 | `common/board_f.c`, `common/board_r.c` | Relocation-state setup and the late boot banner are scoped to this board's own config, instead of affecting the generic code path. | medium, fixed | yes, Stage-2 | [BOARD] [STATIC]: physical boot; banner as the console's first line. |
| F12 | `drivers/net/rtl838x_eth.c` | Frees earlier DMA allocations when a later one in the same probe fails. | medium, fixed | yes, Stage-2 | [BOARD] [STATIC]: physical boot; the allocation failure itself was not induced. |
| F13 | `arch/mips/mach-rtl-otto/preloader/sg1002_mr_gpl_cali.c` | Removed inactive diagnostic leftovers; the functional calibration path stayed byte-identical. | low, cleanup | yes, Stage-1 | [BOARD] [STATIC]: physical reread and OpenWrt boot. |
| F14 | `drivers/net/rtl838x_eth.c` | DMA ring/buffer setup moved to the uncached KSEG1 alias, but stale dirty KSEG0 cache lines (Stage-1 sets `Config.K0=3`, cacheable non-coherent) could still overwrite the descriptors written through KSEG1. **Confirmed on the board**: the first large TFTP transfer after a reset with two links stalled, repeating `ACK 10` for `DATA 11`; `md.l` showed valid descriptors in the RX ring through KSEG1 and stale environment bytes at the same physical address through KSEG0. Fixed by flushing and invalidating the four DMA allocations before using their KSEG1 aliases. | high, fixed | yes, Stage-2 | [BOARD]: aliases matched after the fix; three first transfers after reset, with two links, passed; Stage-2 reread matched and OpenWrt booted. |
| F15 | `arch/mips/Kconfig`, `board/intelbras/sg1002_mr/Kconfig` | The Kconfig `default` for CPU ISA and `TEXT_BASE` disagreed with what the board's own defconfig actually selects (it explicitly overrides them); a stale comment even claimed the RTL8380's core was "MIPS32 Release 1, not R2" although Stage-1 uses R2-only `ext`/`ins` and the board demonstrably runs it. Corrected the defaults and the comment to match the defconfig and the observed silicon. | medium, cosmetic | no, outside the defconfig's own explicit values | [STATIC]: `olddefconfig` on a copy with the explicit options removed now reproduces MIPS32r2, the real `TEXT_BASE`, and the payload mode on its own. |
| F16 | `drivers/serial/serial_rtl8380.c` | Register mapping in the high byte of 32-bit words, the 200 MHz/16/baud divisor, and `-EAGAIN` on a busy TX/RX all match the live board and the Linux DTS (`reg-io-width=<1>`, `reg-shift=<2>`). An intermittent lockup with a non-default baud rate saved to the environment has no isolated cause. | medium, open (see F24) | no | [BOARD] [EMU] [STATIC]: one physical sample of 6/7 resets at 9600 baud reaching the console and 3/3 at 115200; 1/7 is a sample, not an estimated failure rate. |
| F17 | `arch/mips/mach-rtl-otto/rtl8231.c` | Init, soft reset, the READY code, MDIO pinmux enable, and GPIO mode/direction/data all agree with the Linux RTL8231 driver and the datasheet. One real gap: the `LED_START=1` (warm) path could inherit `En_Sync_GPIO=1` (latched GPIO updates) from a previous firmware and then write GPIO outputs without the required latch pulse. Fixed by clearing that bit on both the warm and cold paths. | medium, fixed | yes, Stage-2 | [BOARD] [EMU] [STATIC]: RTL8231 rev. 1.2 datasheet p. 46, `FUNC0[15]`; Linux `803-pinctrl-Add-RTL8231-pin-control-and-GPIO-support.patch`, `rtl8231_configure_safe()`; 23/23 emulator scenarios, three resets, TFTP and OpenWrt. The inherited `FUNC0[15]=1` condition itself was not induced on the board. |
| F18 | `arch/mips/mach-rtl-otto/rtl838x_led.c` | Masks, channel groups, port numbering, and the software-on value (`5`) all match the Linux `gpio-rtl8380-portled` driver; no defect found on review. | low | no | [BOARD] [STATIC]: Linux `850-gpio-rtl8380-port-led-test.patch`; the SYS/amber/green/off lamp-test sequence confirmed visually on the board. |
| F19 | `drivers/serial/serial-uclass.c` | The `UCLASS_SERIAL` device count loop and its empty `else` are dead debugging leftovers; the counted value is never used. The functional logic (skip searching for a console before relocation; do not reprogram the divisor a second time in probe) is sound and unchanged. | low, cleanup debt | no this pass; removing it changes Stage-2 | [BOARD] [STATIC]: kept as-is, since removing 32 otherwise-inert bytes is not worth another hardware round for its own sake. |
| F20 | `arch/mips/lib/reloc.c` | The `putc('R'/'C'/'F'/'J')` markers run before the console exists and are not visible in this configuration. Copy, relocation fixups, cache flush, BSS clearing, and the jump to the relocated address itself all follow the normal U-Boot flow. | low | no this pass; removing it changes Stage-2 | [BOARD] [STATIC]: kept as-is for the same reason as F19. |
| F21 | `arch/mips/mach-rtl-otto/preloader/sg1002_mr_sram.S` | The wait for `D3ZQCCR[31]` (ZQ calibration done) is bounded and proceeds on timeout; the vendor GPL SDK waits for that bit with no bound at all. Deliberate divergence to avoid a permanent hang on a model or a marginal board, but the timeout path itself has never been measured on hardware. | medium, known risk, not fixed | no | [STATIC] [EMU]: compared against the SDK's `plr_dram_gen2.c` wait loop; no physical measurement of the bounded path firing. |
| F22 | `arch/mips/mach-rtl-otto/preloader/sg1002_mr_gpl_cali.c` | The 32x32 GPL scan (resolution 2), the x8 bit mapping, largest-window selection, the tap-placement formula, and the per-DQ PHY commit all agree with the SDK, except for the two deliberate deviations recorded under "DDR training, phase by phase" below. The actual scanned window widths (`wl`/`rl`) on this board were never captured for the record. | medium, margin not measured | no | [STATIC] [BOARD]: `plr_memctl_cali_dram.c` and `plr_plat_dep.c` compared line by line; a passing calibration and a successful boot prove the scan cleared the pass/fail threshold, not how wide the margin actually is. |
| F23 | `drivers/net/rtl838x_eth.c`, `board/intelbras/sg1002_mr/sg1002_mr.c` | Added a one-time boot-time snapshot of link state and resolved speed for all ten front-panel LEDs, from the same registers the Linux GPL PCS driver uses (`MAC_LINK_STS` at `0xa188`, `MAC_LINK_SPD_STS` at `0xa190`). An unresolved copper speed shows amber with a console warning instead of guessing. | feature, delivered | yes, Stage-2 and the environment | [BOARD] [EMU] [STATIC]: on the board, MAC 8 resolved to 1000 Mb/s and MAC 14 to 100 Mb/s; three resets, TFTP, and OpenWrt boot. The emulator has no Ethernet, so it only shows all ten LEDs off. |
| F24 | `drivers/serial/serial_rtl8380.c` | `rtl8380_uart_pending(dev, false)` checks `LSR.THRE` (bit 5); every comparable U-Boot 16550-family driver (`ns16550.c`, `serial_mt7620.c`, `serial_uniphier.c`) checks `LSR.TEMT` (bit 6) for that direction -- THRE only means the holding register emptied, not that the byte actually left the wire. | low, currently inert | no | [STATIC]: `include/serial.h`'s own contract for `pending()`; `_serial_flush()` (`drivers/serial/serial-uclass.c`) is the only caller with `input=false`, and `CONFIG_CONSOLE_FLUSH_SUPPORT` is off in this board's defconfig, so `serial_flush()` compiles to the empty stub in `include/serial.h` and this path never runs today. Would matter again if that option is ever turned on. |
| F25 | `drivers/net/rtl838x_eth.c` | First attempt at fixing F04's fixed 5 s wait: exit the wait loop once every currently linked port also reported `BMSR_ANEGCOMPLETE` and held that state for five consecutive polls. **Reopened F04's own bug by a different path, confirmed on the board twice**, with both cables verified present each time by `ping` right before flashing: only the 100 Mb/s port ever showed up, in 2.3-2.4 s; the 1000 Mb/s port never showed even a bare link bit, not just an incomplete negotiation. Reverted. | high, reverted | no (reverted) | [BOARD]: two clean physical trials, same result both times; `check-sg1002-mr-repro.py` confirms the revert reproduces the pre-attempt reference exactly. |
| F26 | `drivers/net/rtl838x_eth.c` | **The actual fix for F04/F25.** Instead of picking a single "linked" port from a still-settling snapshot, `rtl838x_eth_start()` now floods recovery traffic (ping/tftpboot/dhcp) to every copper port's bit in the direct-port-mask CPU tag, unconditionally, with the wait loop and the port-selection logic removed entirely. This was in fact the code's original behaviour before an earlier, unrecorded change narrowed the mask to "only the currently linked port(s)" as a precaution -- which is what made F04 and F25 possible in the first place. Checked before writing this: neither the Linux driver for this exact PHY (`PHY_ID_RTL8218B_I`, no `.read_status` override, falls back to the generic `genphy_read_status()`), nor the vendor SDK's PHY driver (`phy_rtl8218b.c`, only an on-demand cable-length test, not a passive signal), nor the RTL8218B-VC datasheet expose a "signal present, not yet negotiated" bit distinct from link status -- the ambiguity behind F04/F25 is real, not a research gap, which is why this fix sends to every port instead of trying to pick one. | high, fixed | yes, Stage-2 | [BOARD] [EMU] [STATIC]: `ping` (4.4 s total) and a 229,376-byte `tftpboot` (4.5 s total, verified byte-for-byte) on the board with no wait and only one of two ports actually linked at the time; the full emulator suite dropped from 6m44s to 3m43s with no regression elsewhere (it has no Ethernet PHY, so it cannot exercise this fix directly). |
| F27 | `drivers/net/rtl838x_eth.c` | Side effect of F26, fixed in three attempts. First: the boot-time `LED link: lanX ...` line started always reading "off" because `rtl838x_eth_link_snapshot()` read the PHY link bit once, *before* its own wait loop, which then only waited for the MAC register to catch up to that already-frozen (usually still zero) value -- rereading the PHY on every iteration of the existing loop fixed it, verified with one cable. Second: with both normal cables connected, one port still read "off" despite `MAC_LINK_STS` showing it linked; widening the loop's timeout from 3 s to 5 s did not help, because the cause was not the timeout size. Third, the actual cause: the loop still exited as soon as whichever ports had shown up in `phy_links` *so far* agreed with the MAC register -- the same mistake as F04/F25, in a third function, since the faster port alone satisfied that condition within about a second, before the slower port had shown anything at all. Fixed by removing the early exit entirely: the function now always spends a fixed 5 s before taking one snapshot. Safe to do unconditionally because this function is called exactly once, at boot -- unlike `rtl838x_eth_start()`, this wait is never repeated on `ping`/`tftpboot`/`dhcp`. | medium, fixed | yes, Stage-2 | [BOARD] [EMU]: with both cables connected, `LED link: lan1 green 1000` and `LED link: lan7 amber 10/100` both printed correctly, and each port's `LED_SW_P_CTRL` register read back matching the log line exactly (`0x00000028`, `0x00000005`); OpenWrt booted with a clean `dmesg`. |

## What is a known, accepted limitation (not a bug to chase)

- **F04/F16's baudrate risk.** Changing `baudrate` away from the compiled
  and saved default of 115200 has a measured (small-sample) risk of
  silencing the console; the compiled default and the saved environment
  both stay at 115200, so this never affects a normal boot, Ethernet, or
  OpenWrt. A blind recovery procedure exists and works in the emulator.
- **DDR training margin, temperature, other silicon lots or boards.** The
  calibration code passes and boots reliably on this one physical unit; it
  has not been validated on a second board, at a different temperature, or
  with a different DRAM/PHY silicon lot. See "DDR training, phase by
  phase" below for exactly what was and was not checked against the SDK.
- **F21's unbounded-in-the-SDK ZQ wait**, bounded here on purpose, never
  measured firing on hardware.
- **The full VxWorks vendor system.** The vendor bootrom's boot handoff was
  tested directly on the board (`bootm 0xb4050000` reaches its
  `monitor#` prompt at 9600 baud), but its filesystem no longer has
  `Switch.bin` (removed from the flash at some point before this project);
  a full VxWorks boot was not attempted, and doing so would need writing
  the factory filesystem back to `0x150000`, which was deliberately not
  done. `Switch.bin` itself was located intact elsewhere and matches the
  original CH341A dump byte-for-byte, if this is ever revisited.
- **RTL8231 cold start.** Only simulated (`LED_START` cleared, then reset);
  a genuine cold power-on of the physical chip was not captured separately
  from a full board power cycle.
- Small pieces of dead/inert code kept for the binary-identity invariant
  (F19, F20, and a few busy-waits and silent `putc()` calls in the Stage-1
  assembly and `board_f.c`): removing any of them changes the flashed
  binary and would need a fresh hardware test for a cleanup with no
  functional payoff.

## What is genuinely not reviewed

- **A full instruction-by-instruction Stage-1 audit** against the vendor
  SDK: every branch's delay slot, and the DDR controller register write
  order beyond the phase-level comparison below. Given how the code
  actually behaves on the board today (see "Status" in `sg1002-mr.rst`),
  this is optional unless the board, its silicon lot, or its operating
  conditions change.
- A second physical unit, to separate "this board's DDR training margin"
  from "the DDR training code is correct in general".

## DDR training, phase by phase

Compared against the vendor's local GPL SDK copy
(`arch/mips/cpu/mips4kec/rtl838x/preloader/platform/current/` in that SDK
tree). The SDK is generic; the DCR/DTR/MR register values and the timing
parameter window come from this specific board's factory capture.
`sg1002_mr_sram.S` uses `.set noreorder`; every explicit branch, call, and
jump has an intentional instruction in its delay slot. This check compared
source and disassembly against the known-good Stage-1; it does not measure
electrical timing.

| Phase | Implementation | Comparison and limits |
|---|---|---|
| CPU, cache, UART, and pre-DDR parameters | `sram_entry`, `cpu_state_base`, `cache_prepare_*`, `cpu_select_cached_k0`, `platform_exact_*` | Order transcribed from the factory trace; not present in `plr_memctl_cali_dram.c`. UART resets to 115200 after the helper runs. The ACK waits have no timeout. |
| Controller and ZQ | `platform_init_done` through `DCDR` | MCR preserves the boot straps and clears IPREF/DPREF; DIDER, DACCR, DCR/DTR, DMCR, `DPCR|=0xb`, DDZQPCR, releasing DMCR, D3ZQCCR, and DCDR all follow the SDK's DDR3/RTL8380 profile and the factory trace. DMCR/DDZQPCR/DCDR waits are bounded. The D3ZQCCR timeout proceeds instead of hanging (F21); the SDK's own ZQ status check was not ported -- a specific, known risk with no observed board failure. |
| DLL, MRS, and baseline | `gpl300_ddr3_dll_reset`, `load_calibrated_baseline` | Uses MR0-MR3 from the factory image, the DLL-off/DLL-on MR1 sequence, MR0 reset, an 0x800-cycle settle, MR1/2/3, and the DACCR bit 4 toggle. The 32 PHY delay words are written in groups, each through the GPL DMCR commit, ten MCR reads, a busy wait, and the DACCR bit 4 off/on toggle with ten more reads. The baseline DQM mask is `0x08000000`. |
| Per-DQ scan | `sg1002_mr_gpl_ddr_calibrate()` | Zeroes a 32x32 map, scans write/read 0..30 in steps of 2, programs the 32 delays, writes the pattern, syncs the buffer, XOR-compares, and remaps to physical x8 bit lanes; picks the largest read window, then the largest write window inside it. The `wl<=7`/`rl<=7` failure threshold is the SDK's own. This port returns a per-bit failure/timeout mask with no retry, where the SDK rescans up to five more times: Stage-1 halts at `E` instead. |
| Tap selection | `select_taps()` | `write = ws+wl/3`, `read_max = rs+rl-1`, `read_center = rs+rl/2`, `read_min = rs`, all masked to 5 bits, with one PHY commit per bit. The SDK's x8 path can instead substitute `ws` from its `static_cal_data` table -- see "Two deliberate deviations" below. |
| DQM | `sg1002_mr_gpl_dqm_calibrate()` | Scans all 32 taps of each byte mask with 16-bit stores over an uncached DDR word, picks the centre of a window of at least three taps, and commits it. The exact matching routine is not implemented in the RTL838x GPL tree consulted (only declared in `plr_dram_gen2_memctl.h`); compared instead against the GPL RTL8686 equivalent (`arch/otto/cpu/rlx5281/rtl8686/rtk_soc/util/memctl/memctl_dram.c:849`), which has the same sequence but mistakenly uses `dqm0==31` when finishing DQM1 -- this port correctly tests `tap==31`. With no window found it writes zero; there is no explicit DQM failure signal, though the subsequent RAM test and full copy comparison could catch one. This is a coverage limit, not proof of margin. |
| Verification and handoff | Uncached samples, buffer sync, a 224 KiB copy, a full comparison, D/I-cache flush, jump to KSEG0 | Beyond the SDK's own calibrator: verifies RAM, copies Stage-2 through KSEG1, checks all 224 KiB before the jump, and halts at `E` for a calibration/commit failure or `Z` for a data mismatch. The later `ddr_trace_probe_loop` is unreachable code. |

### Two deliberate deviations from the SDK

1. **The SPI `0x050000` training pattern: kept as-is.** The SDK uses 32
   fixed words; this port reads 4 KiB from the preserved factory image
   instead, through KSEG1, because the first copy mismatch observed during
   development depended on data at offset `+0x204`. KSEG1 avoids a D-cache
   flush between writing and comparing, while the SDK's own window policy
   is unchanged. This ties calibration quality to the contents of that
   flash sector: erasing or overwriting the vendor VxWorks bootrom there
   would weaken it. That region has not been touched. Changing this
   pattern would need measuring `wl`/`rl` on the board before and after.
2. **The scan's write-phase placement: kept as-is.** The SDK's
   `static_cal_data` is a platform parameter table, not this specific
   boot's actual scan result. This port keeps the window it just measured
   on this board and keeps the SDK's placement formula. Substituting a
   measured value with a table entry, even one extracted from the factory
   image, has no basis without comparing the resulting margins. The result
   is not byte-for-byte identical to the SDK's generic values, although
   training and the full RAM copy both pass on this board.

### Margin and further physical testing needed

The known-good build is silent by design: the 32x32 map and the final
`wl`/`rl` live on the SRAM stack during boot and do not survive the jump to
Stage-2. Building a variant that prints them would not give a physical
measurement on its own; running one would mean flashing a different
Stage-1, or an SRAM-execution path that has not been validated. Re-running
DDR training from Stage-2 in DRAM risks destroying the code that is
currently executing. There is therefore **no physical margin number** to
report without changing the flashed binary, or building an independent
SRAM-execution path first.

### Physical DDR failure test (E path)

A diagnostic Stage-1 variant, built on a separate branch, narrowed only the
two scan loops' bounds to a single real tap pair (`w=0, r=0`); the DDR
comparison, the GPL selection logic, the window threshold, and the existing
`E` branch were left untouched. With a single sampled pair, no window can
exceed the seven-tap minimum, so the calibration is guaranteed to fail.

The known-good Stage-1 was backed up before this test. The diagnostic image
was flashed to `0x000000-0x007fff` only, through TFTP + `sf` from a running
U-Boot prompt, with a full readback comparison before the reset. A reset
produced only the `E` halt marker, with no banner, for the whole observation
window -- confirming the failure path actually halts on real hardware, not
just in the model.

Recovery needed an external CH341A SPI programmer: the chip was removed
from the board, the known-good Stage-1 was written back to
`0x000000-0x007fff` only, and `flashrom` confirmed the write with its own
verification pass. With the chip back on the board, it booted normally and
reached OpenWrt.

This test proves the failure path halts as intended on this board; it does
not measure the normal calibration's margin, and it is not a spontaneous
DDR failure.

# SG1002 MR boot-region reference (current default)

Combines the same Stage-1 and environment as `known-good-v219/` with a
rebuilt Stage-2 that fixes the boot-time `LED link: lanX ...` line failing
to show a real second link. Two earlier attempts at this same fix (widening
a snapshot's timeout, then rebuilding the snapshot itself) both still had it
wrong, for the same underlying reason each time.

Root cause, found only by testing with two real cables at once:
`rtl838x_eth_link_snapshot()`'s wait loop exited as soon as whichever ports
had shown up in `phy_links` *so far* agreed with `mac_links` -- with one
port negotiating faster than the other, that condition went true within
about a second of the faster port alone, and the loop left before the
slower port had shown anything at all. This is the same mistake as F04/F25
in doc/board/intelbras/sg1002-mr-findings.md (deciding from a still-growing
set of linked ports) in a third place. Fixed by removing the early exit:
the function now always spends a fixed 5 s before taking one snapshot, safe
here because `sg1002_led_link_status()` -- its only caller -- runs exactly
once at boot, never on ping/tftpboot/dhcp (those stayed instant and
cable-count-independent since the port-flooding fix, F26).

On 2026-09-25, this was flashed directly on the board: two independent TFTP
downloads of the candidate compared with `cmp.b`, the running Stage-2 read
back and confirmed byte-identical to the previous candidate before touching
anything, erase+write limited to `0x008000-0x03ffff`, readback matching the
RAM buffer written, Stage-1 (32 KiB) and the environment (64 KiB) read back
separately and confirmed untouched.

A reset with both cables connected printed `LED link: lan1 green 1000` and
`LED link: lan7 amber 10/100`, both correct. `md.l` on each port's
`LED_SW_P_CTRL` confirmed the physical registers matched exactly
(`0x00000028`: green bit set for lan1; `0x00000005`: amber bit set for
lan7). Boot to the U-Boot prompt took 14 s (the fixed 5 s wait, paid once,
plus the usual boot time). OpenWrt 6.18.44 booted to a shell with a clean
`dmesg`.

Not run through the full emulator suite for this specific candidate beyond
`boot-console`/`led-test-env`; the emulator has no Ethernet PHY to validate
this fix against regardless.

The `.bin` files are ignored by git; `SHA256SUMS` records their local
hashes. This is only the Stage-2 slice plus a composed full boot region; it
is not a full 16 MiB backup. `artifacts/known-good-v219/` remains the
CH341A-verified full-chip recovery reference.

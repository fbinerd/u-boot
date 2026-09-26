# Known-good SG1002 MR boot region (quiet Stage-1)

Flash 0x000000-0x04ffff as verified on the board on 2026-09-23 after the
Stage-1 console output was silenced: `cmp.b` of the whole region against
`known-good-v219-chip-0x000000-0x04ffff.bin` is identical (327680 bytes), 20 of
20 resets reach the banner with nothing printed before it, and OpenWrt boots.

This is the same Stage-1/Stage-2/env layout as the last traced-Stage-1
checkpoint before it, with one difference: Stage-1
(`preloader-stage1-V219-quiet-9b1fdd23.bin`) has 153 words `sw t1,0(t0)`
(0xad090000) changed to `nop` (0x00000000), which is what silences the
console; everything else is identical.

Restore with the CH341A (board off, clip on the chip):

    gravar_spi_ch341a.sh --image artifacts/known-good-v219/known-good-v219-chip-16m.bin \
        --region 0x000000:0x04ffff --confirm-write

This is the CH341A-verified recovery reference kept for a Stage-1 that will
not boot at all and cannot be recovered through the board's own SPI
controller. `tools/check-sg1002-mr-repro.py` builds the tree and compares it
with this image (Stage-1 and env bit for bit, Stage-2 except the version
string); pass `--ref` to compare against this image specifically instead of
the current default reference (`artifacts/known-good-v238/`).

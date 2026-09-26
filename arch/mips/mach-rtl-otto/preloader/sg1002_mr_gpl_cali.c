/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SRAM-resident port of the RTL838x GPL DDR calibration algorithm.
 *
 * The scan and window-selection rules are adapted from the included GPL
 * SDK's plr_memctl_cali_dram.c and plr_plat_dep.c.  Unlike the SDK, the test
 * pattern comes from the preserved factory image at SPI offset 0x050000 and
 * the selected write phase comes from the scan rather than static_cal_data.
 * This standalone form uses KSEG1 directly and is entered only after the
 * observed board DDR setup.
 */

typedef unsigned int u32;
#define SRAM __attribute__((section(".sram.text"), noinline))
#define REG(a) (*(volatile u32 *)(a))
#define DPHY_CAL_CTRL 0xb8001038u
/* RTL838x SDK memctl.h: MCR 0xb8001000, DCR 0xb8001004, DMCR 0xb800101c.  The
 * factory preloader's commit (factory-mmio-trace-v3.log) reads and rewrites
 * 0xb800101c, pauses with ten reads of 0xb8001000 and polls 0xb800101c bit 31. */
#define DMCR          0xb800101cu
#define MCR           0xb8001000u
#define DACCR         0xb8001500u
#define DPHY_DELAY0   0xb8001510u
#define DCDQMR        0xb8001590u
#define CAL_WORDS      0x400u /* 4 KiB: includes the observed +0x204 failure */
#define CAL_TARGET    0xa1c10000u /* first 64 KiB of the GPL Stage-2 slot */

static void SRAM pause10(void)
{
	u32 i;
	for (i = 0; i != 10; ++i)
		(void)REG(MCR);
}

/* Exact GPL _memctl_update_phy_param() transaction, with a finite timeout. */
static u32 SRAM phy_commit(void)
{
	u32 i, v = REG(DMCR);
	REG(DMCR) = v;
	pause10();
	for (i = 0; i != 0x200000; ++i)
		if (!(REG(DMCR) & 0x80000000))
			break;
	if (i == 0x200000)
		return 1;
	v = REG(DACCR);
	REG(DACCR) = v & ~0x10u;
	pause10();
	REG(DACCR) = v | 0x10u;
	return 0;
}

static u32 SRAM sync_write(void)
{
	u32 i;
	REG(DPHY_CAL_CTRL) = 0x80000000;
	for (i = 0; i != 0x200000; ++i)
		if (!(REG(DPHY_CAL_CTRL) & 0x80000000))
			return 0;
	return 1;
}

static u32 SRAM set_all(u32 w, u32 r)
{
	volatile u32 *p = (volatile u32 *)DPHY_DELAY0;
	u32 i, v = (w << 24) | (r << 8);
	for (i = 0; i != 32; ++i)
		p[i] = v;
	return phy_commit();
}

static void SRAM write_pattern(void)
{
	volatile u32 *p = (volatile u32 *)CAL_TARGET;
	volatile u32 *src = (volatile u32 *)0xb4050000u;
	u32 i;
	/*
	 * Keeps the GPL scan and window policy but uses the actual payload: the
	 * first data-dependent failure showed up at offset 0x204.
	 */
	for (i = 0; i != CAL_WORDS; ++i)
		p[i] = src[i];
}

static u32 SRAM verify_pattern(void)
{
	volatile u32 *p = (volatile u32 *)CAL_TARGET;
	volatile u32 *src = (volatile u32 *)0xb4050000u;
	u32 i, err = 0;
	for (i = 0; i != CAL_WORDS; ++i) {
		err |= p[i] ^ src[i];
		if (err == 0xffffffff)
			break;
	}
	return err;
}

static void SRAM record(u32 err, u32 w, u32 r, u32 map[32][32])
{
	u32 bit, ok = ~err;
	/* GPL x8 physical-bit mapping (DCR confirms x8 on this board). */
	ok = ((ok & 0xff000000) >> 24) | (ok & 0x00ff0000) |
	     ((ok & 0x0000ff00) >> 8) | ((ok & 0x000000ff) << 16);
	for (bit = 0; bit != 32; ++bit)
		map[bit][w] |= ((ok >> bit) & 1) << r;
}

static u32 SRAM select_taps(u32 map[32][32])
{
	u32 bit, fail = 0;
	volatile u32 *p = (volatile u32 *)DPHY_DELAY0;
	for (bit = 0; bit != 32; ++bit) {
		u32 w, r, rs = 0, rl = 0, ws = 0, wl = 0;
		if ((bit > 7 && bit < 16) || bit > 23)
			continue;
		/* This is intentionally the GPL edge accounting, including its +1. */
		for (w = 0; w < 32; w += 2) {
			u32 start = 0, len = 0, searching = 1;
			for (r = 0; r < 32; r += 2) {
				if (searching) {
					if ((map[bit][w] >> r) & 1) { start = r; searching = 0; }
					if (r + 2 >= 31 && 1 > rl) { rl = 1; rs = start; }
				} else if (!((map[bit][w] >> r) & 1)) {
					len = r - start - 2 + 1;
					if (len > rl) { rl = len; rs = start; }
					searching = 1;
				} else if (r + 2 >= 31) {
					len = r - start + 1;
					if (len > rl) { rl = len; rs = start; }
				}
			}
		}
		/* Then largest write window within that read window. */
		for (r = rs; r < rs + rl && r < 32; r += 2) {
			u32 start = 0, len = 0, searching = 1;
			for (w = 0; w < 32; w += 2) {
				if (searching) {
					if ((map[bit][w] >> r) & 1) { start = w; searching = 0; }
					if (w + 2 >= 31 && 1 > wl) { wl = 1; ws = start; }
				} else if (!((map[bit][w] >> r) & 1)) {
					len = w - start - 2 + 1;
					if (len > wl) { wl = len; ws = start; }
					searching = 1;
				} else if (w + 2 >= 31) {
					len = w - start + 1;
					if (len > wl) { wl = len; ws = start; }
				}
			}
		}
		/* The GPL x8 path replaces the scanned write window with the phase
		 * from static_cal_data.  This board port retains the scanned window;
		 * only the placement formula (one third/centre) matches the SDK. */
		p[bit] = (((ws + wl / 3) & 31) << 24) |
			 ((rs + rl - 1) & 31) << 16 |
			 ((rs + rl / 2) & 31) << 8 | (rs & 31);
		if (phy_commit()) return 0x80000000;
		if (wl <= 7 || rl <= 7) fail |= 1u << bit;
	}
	return fail;
}

/* Returns the GPL per-bit window failure mask; bit 31 is an MMIO timeout. */
u32 SRAM sg1002_mr_gpl_ddr_calibrate(void)
{
	u32 map[32][32];
	u32 w, r, i;
	for (w = 0; w != 32; ++w)
		for (r = 0; r != 32; ++r)
			map[w][r] = 0;
	for (w = 0; w < 32; w += 2) {
		for (r = 0; r < 32; r += 2) {
			if (set_all(w, r)) return 0x80000000;
			write_pattern();
			if (sync_write()) return 0x80000000;
			record(verify_pattern(), w, r, map);
		}
	}
	i = select_taps(map);
	return i;
}

/*
 * GPL memctlc_dqm_calibration(), kept separate from the per-DQ scan above.
 * DQM is the controller's byte-write-mask timing and lives in DCDQMR rather
 * than DPHY_DELAY0.  A word store followed by a halfword store is the exact
 * published oracle: each half must become 0xffff while the other stays zero.
 * Return the selected DCDQMR value; bit 31 reports a PHY-commit timeout.
 */
u32 SRAM sg1002_mr_gpl_dqm_calibrate(void)
{
	volatile u32 *w = (volatile u32 *)0xa0000000u;
	volatile unsigned short *h;
	u32 tap, min, max, v;

	*w = 0;
	min = max = 32;
	for (tap = 0; tap < 32; ++tap) {
		REG(DCDQMR) = (REG(DCDQMR) & 0x00ffffffu) | (tap << 24);
		if (phy_commit()) return 0x80000000u;
		h = (volatile unsigned short *)0xa0000000u;
		*h = 0xffffu;
		if (*w != 0xffff0000u) {
			if (min != 32 && max == 32) {
				max = tap - 1;
				if (max - min < 3) min = max = 32;
			}
		} else {
			if (min == 32) min = tap;
			if (tap == 31 && max == 32) max = tap;
		}
		*w = 0;
	}
	if (max - min < 3)
		REG(DCDQMR) &= 0x00ffffffu;
	else
		REG(DCDQMR) = (REG(DCDQMR) & 0x00ffffffu) |
				      (((max + min) / 2) << 24);
	if (phy_commit()) return 0x80000000u;

	*w = 0;
	min = max = 32;
	for (tap = 0; tap < 32; ++tap) {
		REG(DCDQMR) = (REG(DCDQMR) & 0xff00ffffu) | (tap << 16);
		if (phy_commit()) return 0x80000000u;
		h = (volatile unsigned short *)0xa0000002u;
		*h = 0xffffu;
		if (*w != 0x0000ffffu) {
			if (min != 32 && max == 32) {
				max = tap - 1;
				if (max - min < 3) min = max = 32;
			}
		} else {
			if (min == 32) min = tap;
			if (tap == 31 && max == 32) max = tap;
		}
		*w = 0;
	}
	if (max - min < 3)
		REG(DCDQMR) &= 0xff00ffffu;
	else
		REG(DCDQMR) = (REG(DCDQMR) & 0xff00ffffu) |
				      (((max + min) / 2) << 16);
	if (phy_commit()) return 0x80000000u;

	v = REG(DCDQMR);
	return v;
}

/* Diagnostic control: apply the existing DQ16 word with the same GPL C
 * transaction used by select_taps(), without changing any timing field. */
u32 SRAM sg1002_mr_gpl_recommit_dq16(void)
{
	u32 v = REG(DPHY_DELAY0 + 64u);
	REG(DPHY_DELAY0 + 64u) = v;
	if (phy_commit()) return 0x80000000u;
	return v;
}

/* Program both DQM delays as one candidate and commit with the same GPL
 * transaction.  Used for a compact Stage-2 prefix matrix after per-DQ
 * calibration has completed. */
u32 SRAM sg1002_mr_gpl_set_dqm_pair(u32 dqm0, u32 dqm1)
{
	u32 v = REG(DCDQMR);
	v = (v & 0x0000ffffu) | ((dqm0 & 31u) << 24) |
	    ((dqm1 & 31u) << 16);
	REG(DCDQMR) = v;
	if (phy_commit()) return 0x80000000u;
	return REG(DCDQMR);
}

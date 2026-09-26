/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SG1002 MR SRAM stage.
 *
 * This is a fresh implementation derived from observable hardware behavior:
 * the NOR stub puts this payload in internal SRAM, and the factory boot path
 * exposes the DDR controller register values and ordering.  No vendor object
 * code or source is used here.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

	.set	noreorder
	.section .sram.text, "ax"
	.globl	sram_entry

#define UART0		0xb8002000
#define MCR		0xb8001000
#define DCR		0xb8001004
#define DTR0		0xb8001008
#define DTR1		0xb800100c
#define DTR2		0xb8001010
#define DMCR		0xb800101c
#define DIDER		0xb8001050
#define DCDR		0xb8001060
#define DPHY_CAL_CTRL	0xb8001038
#define DDZQPCR		0xb8001094
#define D3ZQCCR		0xb8001080	/* DDR3 ZQ long calibration control (SDK D3ZQCCR) */
#define DPCR		0xb8003a00	/* DRAM pad control (SDK DPCR) */
#define DACCR		0xb8001500
#define DACSPCR	0xb8001504
#define DACSPAR	0xb8001508
#define DPHY_DELAY0	0xb8001510
#define DCDQMR		0xb8001590
#define SYSCFG_PARAM_CTRL	0xbb000fc0
#define SYSCFG_PARAM_DATA0	0xbb000fc4
#define SYSCFG_PARAM_DATA1	0xbb000fc8
#define SYSCFG_PARAM_DATA2	0xbb000fcc
#define SYSCFG_PARAM_DATA3	0xbb000fdc
#define SYSCFG_PARAM_DATA4	0xbb000fe0
#define SYSCFG_PARAM_DATA5	0xbb000fe4
#define DDR_AUX_CTRL	0xb8003600
#define WDT_INTR	0xb8003154
#define WDT_CTRL	0xb8003158
#define WDT_INTR_CLEAR	0xc0000000
#define WDT_CTRL_DISABLED	0x00000001

#define UBOOT_FLASH	0xb4050040	/* skip 64-byte uImage header */
#define UBOOT_DDR_LOAD	0xa1c00000	/* uImage load address (uncached) */
#define UBOOT_DDR_ENTRY	0x81c00000	/* uImage entry point (cached) */
#define UBOOT_COPY_LEN	0x000b0000	/* 720KB: uImage data=652KB (0xa2e54), rounded up */

/*
 * Stage-2 lives at SPI 0x008000: the 224 KiB between our 32 KiB Stage-1 and
 * the 0x040000 environment.  That leaves the vendor VxWorks image at 0x050000
 * untouched.
 */
#define GPL_UBOOT_FLASH	0xb4008000	/* SPI offset 0x008000 */
#define GPL_UBOOT_COPY_LEN	0x00038000	/* full 0x008000-0x03ffff window (224 KiB) */

/*
 * Stage-2 is linked at CONFIG_TEXT_BASE 0x83f01000 (System.map: 83f01000 T
 * _start): it is loaded through the uncached alias and entered through the
 * cached one.
 */
#define GPL_UBOOT_DDR_LOAD	0xa3f01000	/* uncached KSEG1, phys 0x03f01000 */
#define GPL_UBOOT_DDR_ENTRY	0x83f01000	/* cached KSEG0, this build's _start */

/*
 * UART0 accepts each NS16550 byte in bits 31:24 of its naturally aligned
 * 32-bit register word.  Bit 5 of LSR is correspondingly bit 29 of a word
 * read.  A normal THRE wait is required for the 2.8 KiB DDR error-matrix
 * transcript.  On this board, the otherwise successful parameter helper can
 * leave that status bit deasserted even though direct UART writes transmit;
 * the optional bounded fallback avoids turning a diagnostic marker into an
 * infinite loop.
 */
/*
 * The one store that puts a byte on the wire.  With SG1002_MR_PRELOADER_QUIET
 * it is a nop of the same size, so everything around it -- the code layout,
 * the THRE wait or the fixed settle loop that follows -- and therefore the
 * timing of the whole boot stays exactly what it is with the console on.
 * Only "loud" bytes (failure markers) are still sent.
 */
.macro uart_tx_store loud=0
#ifdef SG1002_MR_PRELOADER_QUIET
	.if \loud
	sw	t1, 0(t0)
	.else
	nop
	.endif
#else
	sw	t1, 0(t0)
#endif
.endm

.macro uart_put_reg reg, loud=0
	/* The caller may have used t0 for DDR, NOR or RAM immediately before. */
	li	t0, UART0
	/* Direct TX only: LSR may itself be non-responsive after the helper. */
	sll	t1, \reg, 24
	uart_tx_store \loud
	/* At 115200 baud a 10-bit frame takes about 87 us.  The former 0x4000
	 * spin was shorter than a frame on this CPU and dropped diagnostic bytes. */
	li	t2, 0x20000
.Luart_raw_settle\@:
	addiu	t2, t2, -1
	bnez	t2, .Luart_raw_settle\@
	 nop
	b	.Luart_sent\@
	 nop
.Luart_sent\@:
.endm

.macro putc value
	li	t1, \value
	uart_put_reg t1
.endm

/* Like putc, but also sent with SG1002_MR_PRELOADER_QUIET: for failures. */
.macro putc_loud value
	li	t1, \value
	uart_put_reg t1, 1
.endm

/*
 * The factory's post-platform UART divisor 0x0515 establishes an about
 * 200 MHz UART input clock (9600 * 16 * 0x0515 = 199833600).  Divisor 109
 * is therefore the nearest 115200-baud setting after that platform
 * transition.  This is a
 * GPL console policy, deliberately distinct from the factory's 9600 policy.
 */
.macro uart_reinit_post_platform_115200
	li	t0, UART0
	sw	zero, 0x04(t0)		/* IER */
	li	t1, 0x83000000
	sw	t1, 0x0c(t0)		/* DLAB + 8N1 */
	li	t1, 0x6d000000
	sw	t1, 0x00(t0)		/* DLL = 109 */
	sw	zero, 0x04(t0)		/* DLM = 0 */
	li	t1, 0x03000000
	sw	t1, 0x0c(t0)		/* DLAB off, 8N1 */
	sw	t1, 0x10(t0)		/* MCR = DTR | RTS */
	li	t1, 0xc1000000
	sw	t1, 0x08(t0)		/* FIFO enable and clear */
	li	t1, 0x43000000
	sw	t1, 0x10(t0)		/* observed post-platform MCR */
.endm

/*
 * Initialize the indexed cache tags over the 16 KiB range used by the observed
 * factory SRAM routine.  Cache-tag operations only: no load from or store to
 * external DDR.
 */
.macro cache_prepare_observed_range
	mtc0	zero, $28		/* TagLo */
	li	t0, 0x80000000
	li	t1, 0x80004000
.Lcache_tag_store_a\@:
	cache	0x08, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_tag_store_a\@
	 nop
	li	t0, 0x80000000
.Lcache_wbinv\@:
	cache	0x14, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_wbinv\@
	 nop
	li	t0, 0x80000000
.Lcache_tag_store_b\@:
	cache	0x08, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_tag_store_b\@
	 nop
.endm

/* First, non-dereferencing pass of the next observed cache routine. */
.macro cache_prepare_secondary_tags
	mtc0	zero, $28		/* TagLo */
	li	t0, 0x80000000
	li	t1, 0x80004000
.Lcache_secondary_tags\@:
	cache	0x09, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_secondary_tags\@
	 nop
.endm

/*
 * Write back and invalidate the complete 16 KiB data cache by index.  The
 * RTL838x GPL calibrator tests through KSEG0, then performs this operation
 * before both the controller-side verification and the next candidate.  It
 * is a cache coherency operation, not a replacement for PHY programming.
 */
.macro gpl_dcache_writeback_invalidate_all
	li	t0, 0x80000000
	li	t1, 0x80004000
.Lgpl_dcache_wbinv\@:
	/* Index Writeback Invalidate D: invalidate every 16 KiB L1 D-cache set. */
	cache	0x01, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lgpl_dcache_wbinv\@
	 nop
	sync
.endm

/*
 * Stage-2 is copied by the KSEG1 (uncached) alias then entered by KSEG0.
 * The 4KEc does not make that uncached store visible to a stale I-cache line.
 * Invalidate all 16 KiB I-cache indexes while the Stage-1 SRAM code still
 * executes, so the KSEG0 entry refetches the just-copied GPL U-Boot bytes.
 */
.macro gpl_icache_invalidate_all
	li	t0, 0x80000000
	li	t1, 0x80004000
.Lgpl_icache_inv\@:
	cache	0x00, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lgpl_icache_inv\@
	 nop
	sync
	/* Clear any instructions prefetched before the cache operation. */
	nop
	nop
	nop
.endm

/*
 * The factory SRAM entry clears this exact uncached 16 KiB window before its
 * later cache calls.  All writes are volatile DDR locations and no SPI range
 * is touched.
 */
.macro ddr_observed_warmup_clear
	li	t0, 0xa0000000
	li	t1, 0xa0004000
.Lddr_warmup_clear\@:
	sw	zero, 0(t0)
	addiu	t0, t0, 4
	bne	t0, t1, .Lddr_warmup_clear\@
	 nop
.endm

/* Full second factory cache routine, including its line reads. */
.macro cache_prepare_secondary_full
	cache_prepare_secondary_tags
	li	t0, 0x80000000
	li	t1, 0x80004000
.Lcache_secondary_read\@:
	lw	zero, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_secondary_read\@
	 nop
	li	t0, 0x80000000
.Lcache_secondary_tags_final\@:
	cache	0x09, 0(t0)
	addiu	t0, t0, 16
	bne	t0, t1, .Lcache_secondary_tags_final\@
	 nop
.endm

.macro cpu_state_base
	mtc0	zero, $9		/* Count */
	mtc0	zero, $11		/* Compare */
	mtc0	zero, $18		/* WatchLo0 */
	mtc0	zero, $19		/* WatchHi0 */
	mfc0	t0, $12		/* Status */
	li	t1, 0xfffffffe
	and	t0, t0, t1		/* Status.IE = 0 */
	mtc0	t0, $12
.endm

.macro cpu_select_cached_k0
	mfc0	t0, $16		/* Config */
	li	t1, 0xfffffff8
	and	t0, t0, t1
	ori	t0, t0, 3		/* K0 = cacheable, noncoherent (MIPS CCA 3) */
	mtc0	t0, $16
	nop
	nop
	nop
	nop
.endm

/* Emit the low nibble of a register as one lower-case hexadecimal digit. */
.macro puthex_nibble reg
	andi	t1, \reg, 0xf
	sltiu	t2, t1, 10
	bnez	t2, .Lputhex_decimal\@
	 nop
	addiu	t1, t1, 0x57
	b	.Lputhex_print\@
	 nop
.Lputhex_decimal\@:
	addiu	t1, t1, 0x30
.Lputhex_print\@:
	uart_put_reg t1
.endm

/* Emit a 32-bit register as eight lower-case hexadecimal digits. */
.macro puthex32 reg
	move	t4, \reg
	li	t5, 8
.Lputhex32_loop\@:
	srl	t3, t4, 28
	puthex_nibble t3
	sll	t4, t4, 4
	addiu	t5, t5, -1
	bnez	t5, .Lputhex32_loop\@
	 nop
.endm

/* Wait for a controller busy bit to clear, but never hang forever. */
.macro wait_clear address, mask, count, failure
	li	t0, \address
	li	t1, \mask
	li	t2, \count
1:
	lw	t3, 0(t0)
	and	t3, t3, t1
	beqz	t3, 2f
	 nop
	addiu	t2, t2, -1
	bnez	t2, 1b
	 nop
	b	\failure
	 nop
2:
.endm

/*
 * Commit one PHY delay word using the externally observed controller
 * sequence.  The original boot path does this after *each* selected delay
 * word: it reissues the current DMCR value, waits for the controller, then
 * clears and restores DACCR bit 4.  A final static table followed by one
 * global PHY trigger is therefore not equivalent.
 */
.macro phy_delay_commit failure
	li	t0, DMCR
	lw	t1, 0(t0)
	sw	t1, 0(t0)
	li	t2, 10
.Lphy_delay_commit_mcr\@:
	li	t0, MCR
	lw	t3, 0(t0)
	addiu	t2, t2, -1
	bnez	t2, .Lphy_delay_commit_mcr\@
	 nop
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t0, DACCR
	/* Keep the original value.  The GPL _memctl_update_phy_param() writes
	 * (original & ~bit4), waits, then (original | bit4); a second MMIO read
	 * here can observe the controller's transient 0x80000030 state and loses
	 * the DQM latch on the following commit. */
	lw	t4, 0(t0)
	move	t1, t4
	li	t2, 0xffffffef
	and	t1, t1, t2
	sw	t1, 0(t0)
	li	t2, 10
.Lphy_delay_commit_daccr\@:
	li	t0, MCR
	lw	t3, 0(t0)
	addiu	t2, t2, -1
	bnez	t2, .Lphy_delay_commit_daccr\@
	 nop
	li	t0, DACCR
	ori	t4, t4, 0x10
	sw	t4, 0(t0)
.endm

.macro factory_phy_calibrate failure
	li	t0, DPHY_CAL_CTRL
	li	t1, 0x80000000
	sw	t1, 0(t0)
	wait_clear DPHY_CAL_CTRL, 0x80000000, 0x200000, \failure
.endm

/* GPL RTL838x 300 MHz DDR3 DLL/MRS/FIFO reset, written afresh in assembly. */
.macro gpl300_ddr3_dll_reset failure
	li	t0, DMCR
	li	t1, 0x00110041
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t1, 0x00110040
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t1, 0x00101320
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t2, 0x800
.Lgpl300_dll_settle\@:
	li	t0, MCR
	lw	t3, 0(t0)
	addiu	t2, t2, -1
	bnez	t2, .Lgpl300_dll_settle\@
	 nop
	li	t0, DMCR
	li	t1, 0x00110040
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t1, 0x00120400
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t1, 0x00130000
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, \failure
	li	t0, DACCR
	lw	t1, 0(t0)
	li	t2, 0xffffffef
	and	t1, t1, t2
	sw	t1, 0(t0)
	ori	t1, t1, 0x10
	sw	t1, 0(t0)
.endm

/* Load the four observed PHY groups, committing each word independently. */
.macro load_static_phy_delays failure
	li	t4, DPHY_DELAY0
	li	t5, 0x0a1e0f00
	li	t6, 8
.Lstatic_group0\@:
	sw	t5, 0(t4)
	phy_delay_commit \failure
	addiu	t4, t4, 4
	addiu	t6, t6, -1
	bnez	t6, .Lstatic_group0\@
	 nop
	li	t5, 0x1e001e00
	li	t6, 8
.Lstatic_group1\@:
	sw	t5, 0(t4)
	phy_delay_commit \failure
	addiu	t4, t4, 4
	addiu	t6, t6, -1
	bnez	t6, .Lstatic_group1\@
	 nop
	li	t5, 0x001e0f00
	li	t6, 8
.Lstatic_group2\@:
	sw	t5, 0(t4)
	phy_delay_commit \failure
	addiu	t4, t4, 4
	addiu	t6, t6, -1
	bnez	t6, .Lstatic_group2\@
	 nop
	li	t5, 0x1e001e00
	li	t6, 8
.Lstatic_group3\@:
	sw	t5, 0(t4)
	phy_delay_commit \failure
	addiu	t4, t4, 4
	addiu	t6, t6, -1
	bnez	t6, .Lstatic_group3\@
	 nop
.endm

/*
 * Load the calibrated baseline derived from a diagnostic run on the hardware:
 * Group 0: w=14, max=30, cur=30, min=0 (0x0e1e1e00) - opens DQ7 Rising (bit 31)
 * Group 1: static (0x1e001e00)
 * Group 2: Word 16=0x101e1e00, Word 17=0x101e1e00, Word 18=0x101e1e00,
 *          Word 19=0x001e0f00, Word 20=0x001e0f00, Word 21=0x181e1e00,
 *          Word 22=0x101e1e00, Word 23=0x001e1e00 (max=30, cur=30)
 * Group 3: static (0x1e001e00)
 * DCDQMR: 0x08000000 (tap 8)
 */
.macro load_calibrated_baseline failure
	load_static_phy_delays \failure
	li	t4, DPHY_DELAY0
	li	t5, 0x0e1e1e00
	li	t6, 8
.Lcalib_g0\@:
	sw	t5, 0(t4)
	addiu	t4, t4, 4
	addiu	t6, t6, -1
	bnez	t6, .Lcalib_g0\@
	 nop

	li	t4, DPHY_DELAY0 + 64
	li	t5, 0x101e1e00
	sw	t5, 0(t4)
	li	t5, 0x101e1e00
	sw	t5, 4(t4)
	li	t5, 0x101e1e00
	sw	t5, 8(t4)
	li	t5, 0x001e0f00
	sw	t5, 12(t4)
	li	t5, 0x001e0f00
	sw	t5, 16(t4)
	li	t5, 0x181e1e00
	sw	t5, 20(t4)
	li	t5, 0x101e1e00
	sw	t5, 24(t4)
	li	t5, 0x001e1e00
	sw	t5, 28(t4)

	phy_delay_commit \failure
	li	t0, DCDQMR
	li	t1, 0x08000000
	sw	t1, 0(t0)
	factory_phy_calibrate \failure
.endm

/* Return the OR of XOR errors across 256 deterministic uncached RAM words. */
.macro ddr_error_sample synchronize=1
	li	t0, 0xa0000000
	li	t5, 0x1a2b3c4d
	li	t6, 256
.Lsample_write\@:
	sw	t5, 0(t0)
	sll	t1, t5, 13
	xor	t5, t5, t1
	srl	t1, t5, 17
	xor	t5, t5, t1
	sll	t1, t5, 5
	xor	t5, t5, t1
	addiu	t0, t0, 4
	addiu	t6, t6, -1
	bnez	t6, .Lsample_write\@
	 nop
	sync
.if \synchronize
	li	t0, DPHY_CAL_CTRL
	li	t1, 0x80000000
	sw	t1, 0(t0)
	wait_clear DPHY_CAL_CTRL, 0x80000000, 0x200000, ddr_error
.endif
	ddr_error_readback
.endm

.macro ddr_error_readback
	li	t0, 0xa0000000
	li	t4, 0x1a2b3c4d
	move	t5, zero
	li	t6, 256
.Lsample_read\@:
	lw	t1, 0(t0)
	xor	t1, t1, t4
	or	t5, t5, t1
	sll	t1, t4, 13
	xor	t4, t4, t1
	srl	t1, t4, 17
	xor	t4, t4, t1
	sll	t1, t4, 5
	xor	t4, t4, t1
	addiu	t0, t0, 4
	addiu	t6, t6, -1
	bnez	t6, .Lsample_read\@
	 nop
.endm

sram_entry:
	/*
	 * UART0 at 115200 baud.  The observed 200 MHz input clock requires
	 * divisor 0x006d.  Keeping every GPL stage at 115200 makes its first
	 * output readable with the same terminal configuration as GPL U-Boot.
	 */
	li	t0, UART0
	sw	zero, 0x04(t0)      /* IER */
	li	t1, 0x03000000
	sw	t1, 0x10(t0)       /* MCR = DTR | RTS */
	li	t1, 0xc1000000
	sw	t1, 0x08(t0)       /* FIFO enable and clear */
	li	t1, 0x83000000
	sw	t1, 0x0c(t0)       /* DLAB + 8N1 */
	li	t1, 0x6d000000
	sw	t1, 0x00(t0)       /* DLL */
	sw	zero, 0x04(t0)      /* DLM */
	li	t1, 0x03000000
	sw	t1, 0x0c(t0)       /* DLAB off, 8N1 */
	putc	'D'


	/* Exact ordering of the first factory SRAM helper, before platform MMIO. */
	cpu_state_base
	putc	'C'
	ddr_observed_warmup_clear
	putc	'W'
	cache_prepare_observed_range
	putc	'K'
	cache_prepare_secondary_full
	putc	'Q'
	cpu_select_cached_k0
	putc	'U'


	/*
	 * Pre-DRAM transaction observed on the factory boot path.  These writes
	 * precede MCR/DTR setup and are not part of PHY training.  They target the
	 * RTL8380 system-configuration parameter window and its final commit.
	 */
	/*
	 * Clean-room transcription of the observed parameter-window helper at factory
	 * NOR addresses bfc00e1c..bfc00ef4.  The direct factory trace establishes the
	 * second group control sequence as 0x1100 -> 0x1104.
	 *
	 * It stops at the helper return boundary and makes neither the
	 * auxiliary-controller write nor any DDR access.
	 */
	li	t0, SYSCFG_PARAM_CTRL
	lw	t1, 0(t0)
	li	t2, 0xffffefff
	and	t1, t1, t2
	sw	t1, 0(t0)
	lw	t1, 0(t0)
	li	t2, 0xfffffffe
	and	t1, t1, t2
	sw	t1, 0(t0)

	li	t1, 0x2da0cfb8
	li	t0, SYSCFG_PARAM_DATA2
	sw	t1, 0(t0)
	li	t1, 0x00004748
	li	t0, SYSCFG_PARAM_DATA0
	sw	t1, 0(t0)
	li	t1, 0x0c14530e
	li	t0, SYSCFG_PARAM_DATA1
	sw	t1, 0(t0)

	li	t0, SYSCFG_PARAM_CTRL
	lw	t1, 0(t0)
	ori	t1, t1, 0x0001
	sw	t1, 0(t0)
	li	t2, 0x00000100
platform_exact_step1_wait:
	lw	t1, 0(t0)
	and	t1, t1, t2
	beqz	t1, platform_exact_step1_wait
	 nop

	/*
	 * The physical parameter engine is cadence-sensitive: this fixed settle
	 * interval (no register or serial access) is needed before its second
	 * transaction.
	 */
	li	t2, 0x4000
platform_exact_first_settle:
	addiu	t2, t2, -1
	bnez	t2, platform_exact_first_settle
	 nop
	li	t0, SYSCFG_PARAM_CTRL

	lw	t1, 0(t0)
	ori	t1, t1, 0x1000
	sw	t1, 0(t0)

	/*
	 * Literal factory helper: the second branch clears bit 2, yielding the
	 * observed 0x1100 state, before it writes DATA5/3/4 and submits 0x1104.
	 */
	lw	t1, 0(t0)
	li	t2, 0xfffffffb
	and	t1, t1, t2
	sw	t1, 0(t0)
	li	t2, 0x4000
platform_exact_second_settle:
	addiu	t2, t2, -1
	bnez	t2, platform_exact_second_settle
	 nop

	li	t1, 0x2da0cfb8
	li	t0, SYSCFG_PARAM_DATA5
	sw	t1, 0(t0)
	li	t1, 0x000041bc
	li	t0, SYSCFG_PARAM_DATA3
	sw	t1, 0(t0)
	li	t1, 0x14018c80
	li	t0, SYSCFG_PARAM_DATA4
	sw	t1, 0(t0)

	li	t0, SYSCFG_PARAM_CTRL
	lw	t1, 0(t0)
	ori	t1, t1, 0x0004
	sw	t1, 0(t0)
	li	t2, 0x00000400
	/* Allow the second command edge to reach the parameter engine. */
	li	t2, 0x4000
platform_exact_launch_settle:
	addiu	t2, t2, -1
	bnez	t2, platform_exact_launch_settle
	 nop
	li	t0, SYSCFG_PARAM_CTRL
	li	t2, 0x00000400
platform_exact_step2_wait:
	lw	t1, 0(t0)
	and	t1, t1, t2
	beqz	t1, platform_exact_step2_wait
	 nop
	/*
	 * The factory path reprograms UART0 around this boundary: set it to 115200
	 * again after the platform transition.
	 */
	uart_reinit_post_platform_115200
	putc	'X'
	/* Continue at the common DDR boundary. */
	b	platform_init_done
	 nop

	li	t0, SYSCFG_PARAM_CTRL
	/*
	 * The factory code preserves all unrelated control fields.  It clears
	 * bit 12 first and bit 0 second; a raw zero write is not equivalent and
	 * was shown to disrupt the physical board.
	 */
	lw	t1, 0(t0)
	li	t2, 0xffffefff
	and	t1, t1, t2
	sw	t1, 0(t0)
	lw	t1, 0(t0)
	li	t2, 0xfffffffe
	and	t1, t1, t2
	sw	t1, 0(t0)
	li	t1, 0x2da0cfb8
	li	t0, SYSCFG_PARAM_DATA2
	sw	t1, 0(t0)
	li	t1, 0x00004748
	li	t0, SYSCFG_PARAM_DATA0
	sw	t1, 0(t0)
	li	t1, 0x0c14530e
	li	t0, SYSCFG_PARAM_DATA1
	sw	t1, 0(t0)

	li	t0, SYSCFG_PARAM_CTRL

	/* Submit the first data group by setting bit 0, then poll bit 8. */
	lw	t1, 0(t0)
	ori	t1, t1, 0x0001
	sw	t1, 0(t0)
	li	t2, 0x00000100
platform_step2_wait:
	lw	t1, 0(t0)
	and	t1, t1, t2
	beqz	t1, platform_step2_wait
	 nop


	/* Preserve the acknowledgement bit while enabling the next control bit. */
	lw	t1, 0(t0)
	ori	t1, t1, 0x1000
	sw	t1, 0(t0)

	lw	t1, 0(t0)
	ori	t1, t1, 0x1000
	sw	t1, 0(t0)

	li	t1, 0x2da0cfb8
	li	t0, SYSCFG_PARAM_DATA5
	sw	t1, 0(t0)
	li	t1, 0x000041bc
	li	t0, SYSCFG_PARAM_DATA3
	sw	t1, 0(t0)
	li	t1, 0x14018c80
	li	t0, SYSCFG_PARAM_DATA4
	sw	t1, 0(t0)
	li	t0, SYSCFG_PARAM_CTRL


	/* Submit the second data group by setting bit 2, then poll bit 10. */
	lw	t1, 0(t0)
	ori	t1, t1, 0x0004
	sw	t1, 0(t0)
	li	t2, 0x00000400
platform_step4_wait:
	lw	t1, 0(t0)
	and	t1, t1, t2
	beqz	t1, platform_step4_wait
	 nop


	/* Preserve low auxiliary-controller fields exactly as the factory does. */
	li	t0, DDR_AUX_CTRL
	lw	t1, 0(t0)
	li	t2, 0x3fffffff
	and	t1, t1, t2
	sw	t1, 0(t0)

	li	t0, SYSCFG_PARAM_CTRL

	li	t1, 0x00003500
	sw	t1, 0(t0)

	putc	'G'

platform_init_done:

	/*
	 * Initial DDR3 controller setup observed on the original hardware boot
	 * path, following the controller protocol of the GPL-2.0 RTL838x U-Boot
	 * source (plr_plat_dep.c and plr_memctl_ddr3.c) as a fresh assembly
	 * implementation.  The fine PHY sweep is done by the GPL calibration in
	 * sg1002_mr_gpl_cali.c.
	 */
	li	t0, MCR
	/*
	 * MCR also owns flash-map and DRAM-type strap fields.  The published GPL
	 * RTL838x DRAM setup preserves those fields and clears only IPREF/DPREF
	 * (bits 23:22): a literal zero would lose board-owned configuration.
	 */
	lw	t1, 0(t0)
	li	t2, 0xff3fffff		/* ~(MCR_IPREF_MASK | MCR_DPREF_MASK) */
	and	t1, t1, t2
	sw	t1, 0(t0)
	putc	'V'
	puthex32 t1
	putc	'\n'
	li	t0, DIDER
	/* Select the DDR3 profile explicitly. */
	/* The matching GPL 300 MHz board profile retains DIDER bit 31, and so
	 * does the factory (factory-mmio-trace-v3.log: DIDER = 0x80000000). */
	li	t1, 0x80000000
	sw	t1, 0(t0)
	li	t0, DACCR
	li	t1, 0x80000010
	sw	t1, 0(t0)
	li	t0, DACSPCR
	li	t1, 0x0000007f
	sw	t1, 0(t0)
	li	t0, DACSPAR
	sw	zero, 0(t0)
	/*
	 * The exact parameter helper returns before this distinct auxiliary
	 * controller transaction.  The factory preserves its low 30 bits and
	 * clears only the selected upper two bits before the 0x3500 commit.
	 */
	li	t0, DDR_AUX_CTRL
	lw	t1, 0(t0)
	li	t2, 0x3fffffff
	and	t1, t1, t2
	sw	t1, 0(t0)
	/*
	 * The original SRAM stage now performs the final system-control commit
	 * before it writes DTR0/1/2.  The observed 0x3500 is not a literal: at
	 * bfc01ac4 it reads the existing control word and sets bit 13.  Preserve
	 * the state bits, wait for that acknowledgement, then retain the factory's
	 * bounded 0x10000-cycle settle interval.
	 */
	li	t0, SYSCFG_PARAM_CTRL
	lw	t1, 0(t0)
	ori	t1, t1, 0x2000
	sw	t1, 0(t0)
platform_final_ctrl_wait:
	lw	t1, 0(t0)
	andi	t1, t1, 0x2000
	beqz	t1, platform_final_ctrl_wait
	 nop
	li	t2, 0x10000
platform_final_ctrl_settle:
	addiu	t2, t2, -1
	bnez	t2, platform_final_ctrl_settle
	 nop
	putc	'P'
	li	t0, DCR
	li	t1, 0x20320000
	sw	t1, 0(t0)
	li	t0, DTR0
	li	t1, 0x54433830
	sw	t1, 0(t0)
	li	t0, DTR1
	li	t1, 0x0404030f
	sw	t1, 0(t0)
	li	t0, DTR2
	li	t1, 0x0630d000
	sw	t1, 0(t0)
	putc	'1'

	/* Start controller then wait for its self-clearing busy bit. */
	li	t0, DMCR
	li	t1, 0x01000000
	sw	t1, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, ddr_error

	putc	'2'

	/* SDK memctlc_ZQ_calibration() for chip ID 0x62750000 (RTL8380) with
	 * DDR3: DPCR |= 0xb before the ZQ trigger.  The factory does this on real
	 * hardware; the emulator reads chip ID 0 and MCR type != DDR3, so its
	 * trace never shows it. */
	li	t0, DPCR
	lw	t1, 0(t0)
	ori	t1, t1, 0x000b
	sw	t1, 0(t0)
	/* ZQ calibration request observed before the factory MRS sequence. */
	li	t0, DDZQPCR
	li	t1, 0x8000107c
	sw	t1, 0(t0)
	wait_clear DDZQPCR, 0x80000000, 0x200000, ddr_error

	putc	'3'

	/* The observed sequence releases DMCR before programming DCDR. */
	li	t0, DMCR
	sw	zero, 0(t0)
	wait_clear DMCR, 0x80000000, 0x200000, ddr_error
	/* SDK memctlc_DDR3_ZQ_long_calibration(): D3ZQCCR bit 31, ~100 MCR reads,
	 * wait for it to clear.  Bounded, and a timeout just continues, so a
	 * register that never self-clears (emulator) does not stop the flow. */
	li	t0, D3ZQCCR
	lw	t1, 0(t0)
	lui	t2, 0x8000
	or	t1, t1, t2
	sw	t1, 0(t0)
	li	t2, 100
.Lsdk_zqcl_delay:
	li	t3, MCR
	lw	t3, 0(t3)
	addiu	t2, t2, -1
	bnez	t2, .Lsdk_zqcl_delay
	 nop
	wait_clear D3ZQCCR, 0x80000000, 0x200000, .Lsdk_zqcl_done
.Lsdk_zqcl_done:
	li	t0, DCDR
	li	t1, 0x8b540000
	sw	t1, 0(t0)

	/* RTL838x GPL input-clock delay programming requires this settle time. */
	li	t2, 0x200000
ddr_dcdr_readback_wait:
	lw	t3, 0(t0)
	beq	t1, t3, ddr_dcdr_readback_done
	 nop
	addiu	t2, t2, -1
	bnez	t2, ddr_dcdr_readback_wait
	 nop
	b	ddr_error
	 nop
ddr_dcdr_readback_done:
	li	t2, 0x10000
ddr_dcdr_settle_wait:
	li	t0, MCR
	lw	t3, 0(t0)
	addiu	t2, t2, -1
	bnez	t2, ddr_dcdr_settle_wait
	 nop
	putc	'T'


	/* SDK memctlc_ddr3_dll_reset() with the factory's own DRAM parameter
	 * block (SPI dump 0x737c): DDR3_mr0 0x00101220, mr1 0x00110040, mr2
	 * 0x00120400, mr3 0x00130000.  Sequence: MR1 with DLL off, MR1 with DLL
	 * on, MR0 | DLL reset (0x00101320), 0x800 settle, MR1, MR2, MR3, then
	 * the DACCR bit 4 toggle. */
	gpl300_ddr3_dll_reset ddr_error

	putc	'4'

	putc	'V'
	putc	'1'
	putc	'Z'
	putc	'\n'

	load_calibrated_baseline ddr_error


	/* C code and its 4 KiB 32x32 GPL scan matrix live entirely in SRAM. */
	li	sp, 0xbf008000
	jal	sg1002_mr_gpl_ddr_calibrate
	 nop
	putc	'G'
	putc	':'
	puthex32 v0
	putc	'\n'
	/* Never chainload with a failed DQ window or a timed-out PHY commit. */
	bnez	v0, ddr_error
	 nop
	/* The GPL flow calibrates byte-write-mask timing after the per-DQ scan. */
	jal	sg1002_mr_gpl_dqm_calibrate
	 nop
	move	s0, v0
	putc	'D'
	putc	'Q'
	putc	'M'
	putc	':'
	puthex32 s0
	putc	'\n'
	/* DCDQMR is a register value; only this sentinel denotes a timeout. */
	li	t3, 0x80000000
	beq	s0, t3, ddr_error
	 nop
	/* Phase 6: Full 32-bit RAM Verification */
	li	t0, 0xa0000000
	li	t1, 0x12345678
	sw	t1, 0(t0)
	li	t1, 0x9abcdef0
	sw	t1, 4(t0)
	li	t1, 0xa55aa55a
	sw	t1, 8(t0)
	li	t1, 0x5aa55aa5
	sw	t1, 12(t0)
	sync

	li	t0, DPHY_CAL_CTRL
	li	t1, 0x80000000
	sw	t1, 0(t0)
	wait_clear DPHY_CAL_CTRL, 0x80000000, 0x200000, ddr_error

	putc	'R'
	putc	'0'
	putc	':'
	li	t0, 0xa0000000
	lw	s6, 0(t0)
	puthex32 s6
	putc	'\n'

	putc	'R'
	putc	'1'
	putc	':'
	li	t0, 0xa0000000
	lw	s6, 4(t0)
	puthex32 s6
	putc	'\n'

	putc	'X'
	putc	'0'
	putc	':'
	li	t0, 0xa0000000
	lw	s6, 0(t0)
	li	t1, 0x12345678
	xor	s6, s6, t1
	puthex32 s6
	putc	'\n'

	ddr_error_sample
	move	s6, t5
	putc	'L'
	putc	'F'
	putc	':'
	puthex32 s6
	putc	'\n'
	bnez	s6, .Lzero_probe_done
	 nop

.Lzero_probe_pass:
	putc	'!'
	putc	'P'
	putc	'A'
	putc	'S'
	putc	'S'
	putc	'!'
	putc	'\n'

	/* Phase 7: independently built GPL U-Boot stage-2 verification. */
	li	t0, GPL_UBOOT_FLASH
	lw	s7, 0(t0)
	li	t1, GPL_UBOOT_DDR_LOAD
	sw	s7, 0(t1)
	sync
	li	t0, DPHY_CAL_CTRL
	li	t2, 0x80000000
	sw	t2, 0(t0)
	wait_clear DPHY_CAL_CTRL, 0x80000000, 0x200000, ddr_error

	li	t1, GPL_UBOOT_DDR_LOAD
	lw	s6, 0(t1)
	putc	'S'
	putc	'2'
	putc	':'
	puthex32 s6
	putc	'\n'

	bne	s6, s7, .Lzero_probe_done
	 nop
	putc	'!'
	putc	'S'
	putc	'T'
	putc	'A'
	putc	'G'
	putc	'E'
	putc	'2'
	putc	'_'
	putc	'R'
	putc	'E'
	putc	'A'
	putc	'D'
	putc	'Y'
	putc	'!'
	putc	'\n'

	/* Phase 8: chainload GPL U-Boot stage-2 from SPI NOR into DDR. */
	/* The factory bootstrap normally disarms this watchdog before its payload
	 * reaches board_late_init(). Our standalone Stage-1 must do it now: the
	 * GPL U-Boot needs enough time to establish global data and serial. */
	li	t0, WDT_INTR
	li	t1, WDT_INTR_CLEAR
	sw	t1, 0(t0)
	li	t0, WDT_CTRL
	li	t1, WDT_CTRL_DISABLED
	sw	t1, 0(t0)
	sync
	putc	'!'
	putc	'W'
	putc	'D'
	putc	'T'
	putc	'_'
	putc	'O'
	putc	'F'
	putc	'F'
	putc	'!'
	putc	'\n'
	putc	'!'
	putc	'B'
	putc	'O'
	putc	'O'
	putc	'T'
	putc	'!'
	putc	'\n'

	li	t0, UART0
	li	t2, 0x20000000
	/* A stuck THRE bit must not prevent the already verified Stage-2 copy. */
	li	t4, 0x200000
.Luart_wait_final:
	lw	t3, 0x14(t0)
	and	t3, t3, t2
	bnez	t3, .Luart_wait_done
	 nop
	addiu	t4, t4, -1
	bnez	t4, .Luart_wait_final
	 nop
.Luart_wait_done:

	/* Do this before the uncached copy; stale dirty lines must not win later. */
	gpl_dcache_writeback_invalidate_all
	li	t0, GPL_UBOOT_FLASH
	li	t1, GPL_UBOOT_DDR_LOAD
	li	t2, GPL_UBOOT_COPY_LEN
.Lcopy_uboot_loop:
	lw	t3, 0(t0)
	sw	t3, 0(t1)
	addiu	t0, t0, 4
	addiu	t1, t1, 4
	addiu	t2, t2, -4
	bnez	t2, .Lcopy_uboot_loop
	 nop
	sync
	gpl_icache_invalidate_all

	/*
	 * Copy the bounded GPL U-Boot stage-2 slot, verifying both endpoints before
	 * and after the latch.
	 */
	putc	'!'
	putc	'C'
	putc	'O'
	putc	'P'
	putc	'Y'
	putc	'!'
	putc	'\n'

	li	t0, GPL_UBOOT_FLASH
	lw	s6, 0(t0)
	li	t0, GPL_UBOOT_DDR_LOAD
	lw	s7, 0(t0)
	bne	s6, s7, .Lzero_probe_done
	 nop
	li	t0, GPL_UBOOT_FLASH + GPL_UBOOT_COPY_LEN - 4
	lw	s6, 0(t0)
	li	t0, GPL_UBOOT_DDR_LOAD + GPL_UBOOT_COPY_LEN - 4
	lw	s7, 0(t0)
	bne	s6, s7, .Lzero_probe_done
	 nop
	/* Endpoint equality alone cannot detect a damaged interior cache line. */
	li	t0, GPL_UBOOT_FLASH
	li	t1, GPL_UBOOT_DDR_LOAD
	li	t2, GPL_UBOOT_COPY_LEN
.Lverify_uboot_full:
	lw	t3, 0(t0)
	lw	t4, 0(t1)
	bne	t3, t4, .Lzero_probe_done
	 nop
	addiu	t0, t0, 4
	addiu	t1, t1, 4
	addiu	t2, t2, -4
	bnez	t2, .Lverify_uboot_full
	 nop
	putc	'!'
	putc	'C'
	putc	'O'
	putc	'P'
	putc	'Y'
	putc	'_'
	putc	'O'
	putc	'K'
	putc	'!'
	putc	'\n'
	putc	'!'
	putc	'F'
	putc	'U'
	putc	'L'
	putc	'L'
	putc	'_'
	putc	'O'
	putc	'K'
	putc	'!'
	putc	'\n'

	putc	'!'
	putc	'L'
	putc	'A'
	putc	'T'
	putc	'C'
	putc	'H'
	putc	'!'
	putc	'\n'

	li	t0, DPHY_CAL_CTRL
	li	t1, 0x80000000
	sw	t1, 0(t0)
	wait_clear DPHY_CAL_CTRL, 0x80000000, 0x200000, ddr_error

	putc	'!'
	putc	'L'
	putc	'A'
	putc	'T'
	putc	'C'
	putc	'H'
	putc	'_'
	putc	'O'
	putc	'K'
	putc	'!'
	putc	'\n'
	putc	'!'
	putc	'J'
	putc	'U'
	putc	'M'
	putc	'P'
	putc	'!'
	putc	'\n'

	li	t0, GPL_UBOOT_DDR_ENTRY
	jr	t0
	 nop

.Lzero_probe_done:
	putc	'Z'
	putc	'\n'
ddr_zero_done_loop:
	b	ddr_zero_done_loop
	 nop


#undef GROUP2_SCAN_OFFSET
#undef GROUP2_SCAN_TAG


#undef GROUP0_SCAN_OFFSET
#undef GROUP0_SCAN_TAG

#undef GROUP1_SCAN_OFFSET
#undef GROUP1_SCAN_TAG
#undef GROUP1_SCAN_BASE
#undef GROUP1_SCAN_PREFIX


	/* Validate uncached DDR before we copy executable code to cached DDR. */
	li	t0, 0xa0000000
	li	t1, 0x13579bdf
	sw	t1, 0(t0)
	sync
	lw	t2, 0(t0)
	bne	t1, t2, ddr_error
	 nop
	li	t1, 0x2468ace0
	sw	t1, 4(t0)
	sync
	lw	t2, 4(t0)
	bne	t1, t2, ddr_error
	 nop

	putc	'5'

	putc	'R'


	/*
	 * Terminal loop of the DDR bring-up diagnostic.  Nothing branches here; the
	 * code below it is not reachable either.  Both are kept so that the flashed
	 * image is reproduced bit for bit (dropping them is a separate,
	 * binary-changing cleanup).
	 */
ddr_trace_probe_loop:
	b	ddr_trace_probe_loop
	 nop

	putc	'U'
	/* Copy the DDR-linked U-Boot stage from the dedicated test slot. */
	li	t0, UBOOT_FLASH
	/* Store through KSEG1 so cache setup cannot discard this payload. */
	li	t1, UBOOT_DDR_LOAD
	li	t2, UBOOT_COPY_LEN
copy_uboot:
	lw	t3, 0(t0)
	sw	t3, 0(t1)
	addiu	t0, t0, 4
	addiu	t1, t1, 4
	addiu	t2, t2, -4
	bnez	t2, copy_uboot
	 nop
	sync
	li	t0, UBOOT_DDR_ENTRY
	jr	t0
	 nop

ddr_error:
	li	t0, UART0

	putc_loud 'E'
error_loop:
	b	error_loop
	 nop

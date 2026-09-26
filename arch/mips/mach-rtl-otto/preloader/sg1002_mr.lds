/* SPDX-License-Identifier: GPL-2.0+ */
OUTPUT_ARCH(mips)
ENTRY(_start)

MEMORY {
	NOR (rx)   : ORIGIN = 0xbfc00000, LENGTH = 0x8000
	SRAM (rwx) : ORIGIN = 0xbf000000, LENGTH = 0x8000
}

SECTIONS {
	.ucflash ORIGIN(NOR) : {
		KEEP(*(.ucflash))
	} > NOR

	.sram ORIGIN(SRAM) + 0x1200 : AT(ORIGIN(NOR) + 0x1200) {
		__sram_start = .;
		*(.sram.text)
		*(.sram.rodata)
		__sram_payload_end = .;
		. = ORIGIN(SRAM) + 0x76bc;
		__sram_end = .;
	}
	__sram_load_start = LOADADDR(.sram);
	ASSERT(SIZEOF(.ucflash) <= 0x1200, "NOR stub exceeds observed factory boundary")
	ASSERT(SIZEOF(.sram) <= LENGTH(SRAM), "SRAM payload exceeds 32 KiB")
	/* The DDR scan uses a 4 KiB stack frame plus nested calls.  The copied
	 * padding may overlap the stack, but executable code and data must not. */
	ASSERT(__sram_payload_end <= ORIGIN(SRAM) + 0x6000,
	       "SRAM payload overlaps the 8 KiB stack reserve")
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) }
}

/*
 * link.lds
 *
 * Linker script for the i386 SKL setup code
 *
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
ENTRY(_entry)

SECTIONS
{
	. = 0;
	_start = .;
	.text : {
		*(.headers)
		*(.text*)
	}
	.rodata : {
		*(SORT_BY_ALIGNMENT(.rodata*))
		/*
		 * This is unwritten (as of AMD APM revision 3.42) behavior of SKINIT.
		 * PCR 17 is extended with SKL padded with zeros up to a size that is
		 * aligned up to the multiply of 4B. To keep functions in util.sh
		 * simple, _end_of_measured is explicitly aligned. This may increase
		 * skl.bin size by up to 3 bytes, but since next section is aligned to
		 * 4K anyway this doesn't reduce the amount of available space, neither
		 * does it increase the real size of data sent to TPM to be measured.
		 *
		 * Note that this padding must be part of .rodata to be included in
		 * skl.bin, both for 'dd' used in util.sh and for checks performed by
		 * GRUB.
		 */
		. = ALIGN(4);
	}

	_end_of_measured = .;

	.bss : {
		. = ALIGN(4096);
		_bss = .;
		/*
		 * Due to the 64k total size constraint, we link all page size/aligned
		 * data together in a single section, to avoid wasting space in the
		 * individual bss sections.
		 */
		*(SORT_BY_ALIGNMENT(.bss*))
		. = ALIGN(4);
		_ebss = .;
	}

	/*
	 * Bootloader must pass non-constant data (e.g. address of kernel). Keep
	 * it in separate section, outside of measured part of SL. This must be
	 * done in order to keep hashes constant, it also allows us to measure SL
	 * offline.
	 */
	. = ALIGN(4);
	.bootloader_data : {
		*(.bootloader_data)
	}

	_end = .;

	/* This section is expected to be empty. */
	.got : {
		*(.got)
	}

	/* This section is expected to be empty, unless ld is bugged. */
	.got.plt : {
		*(.got.plt)
	}

	/*
	 * This section is expected to be empty. To keep amount of data sent to the
	 * TPM by SKINIT instruction low, all data is either read-only (.rodata), or
	 * uninitialized and built at run time (.bss).
	 */
	.data : {
		*(.data*)
	}

	/DISCARD/ : {
		*(.eh_frame*)
	}
}

ASSERT(_end <= 0x10000, "Landing Zone exceeds 64k");
ASSERT(SIZEOF(.got) == 0, ".got section not empty - non-hidden symbols used?");
ASSERT(SIZEOF(.data) == 0, ".data section not empty - initialized data used?");

/*
 * At least GNU ld 2.30 and earlier fail to discard the generic part of
 * .got.plt when no actual entries were allocated. Permit this case alongside
 * the section being empty.
 */
ASSERT(!SIZEOF(.got.plt) || SIZEOF(.got.plt) == 3 * BITS / 8,
       "unexpected .got.plt size");

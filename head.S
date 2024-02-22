/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * Author:
 *     Ross Philipson <ross.philipson@oracle.com>
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

#include <defs.h>
#include <multiboot2.h>

/* Selectors, CS and SS compatible with initial state after SKINIT */
#define CS_SEL32         0x0008
#define DS_SEL           0x0010
#ifdef __x86_64__
#define CS_SEL64         0x0018
#endif

	.section .headers, "ax", @progbits

GLOBAL(sl_header)
	.word	_entry           /* SL header SKL offset to code start */
	.word	bootloader_data  /* SL header SKL measured length */
	.word	skl_info
ENDDATA(sl_header)

	.text
	.code32

GLOBAL(_entry)
	/*
	 * Per the spec:
	 * %eax	   - Beginning of SKL containing the SL header
	 * %edx	   - Family/Model/Stepping
	 * %esp	   - %eax + 64k (End of SLB)
	 * %cs	   - 32bit Flat, selector 0x08
	 * %ss	   - 32bit Flat, selector 0x10
	 * %ds/etc - 16bit Real Mode segments.	Unusable.
	 *
	 * Restore the world, get back into long mode.
	 *
	 * The GDT needs relocating before we have a usable %ds, which must be
	 * done with an %ss-relative write.  Therefore, we store the base
	 * address in %ebp.
	 */
	mov	%eax, %ebp

	/* Check if we have enough space available for stack. */
	lea	bootloader_data(%ebp), %ebx
	/* TODO: below uses bootloader_data.size, must be changed for SLRT. */
	movzwl	%ss:2(%ebx), %edx
	add	%edx, %ebx
	add	$MAX_STACK_SIZE, %ebx
	cmp	%ebx, %esp
	jge	.Lenough_stack
1:
	/* XXX: is there any way of notifying the user about this? */
	hlt
	jmp	1b
.Lenough_stack:

	/*
	 * Clobber IDTR.limit to prevent stray interrupts/exceptions/INT from
	 * vectoring via the IVT into unmeasured code.
	 */
	push	$0
	push	$0
	lidt	(%esp)

	/* Clear DIS_A20M. R_INIT is cleared by the kernel. */
	mov	$IA32_VM_CR, %ecx
	rdmsr
	and	$~(VM_CR_DIS_A20M), %eax
	wrmsr

	/* Load GDT */
	movl	$gdt, 4(%esp)			/* Base ... */
	add	%ebp, 4(%esp)			/* ... relocated */
	movw	$(.Lgdt_end - gdt - 1), 2(%esp)	/* Limit */
	lgdt	2(%esp)
	add	$8, %esp

	/* Load data segment regs.  %ss is already flat and matches DS_SEL. */
	mov	$DS_SEL, %eax
	mov	%eax, %ds
	mov	%eax, %es

	/* Clear .bss.  Linker scripts ensures multiple of 4B size. */
	xor	%eax, %eax
	cld
	lea	_bss(%ebp), %edi
	lea	_ebss(%ebp), %ecx
	sub	%edi, %ecx
	shr	$2, %ecx
	rep stosl

#ifdef __x86_64__
	/* Relocate 64bit ljmp offset and build pagetables. */
	/* FIXME: self-modifying code can't be self-measuring! */
	add	%ebp, 1 + .Ljump64(%ebp)

	/*
	 * Pagetables are located in .bss that was just cleared, and we are
	 * loaded to <4GB memory, so we don't have to bother with writing to
	 * higher dword of each entry.
	 */
	/* 1x L1 page, 512 entries mapping total of 2M. */
	lea	l1_identmap(%ebp), %edi
	mov	$512, %ecx
	mov	$(_PAGE_AD + _PAGE_RW + _PAGE_PRESENT + 512 * PAGE_SIZE), %edx
.Lfill_l1_identmap:
	sub	$PAGE_SIZE, %edx
	/* Loop runs for ecx=[512..1] for entries [511..0], hence -8. */
	mov	%edx, -8(%edi,%ecx,8)
	loop	.Lfill_l1_identmap

	/* 4x L2 pages, each page mapping 1G of RAM. */
	lea	l2_identmap(%ebp), %edi
	/* 1st entry points to L1. */
	lea	(l1_identmap + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT)(%ebp), %edx
	mov	%edx, (%edi)
	/* Other entries are 2MB pages. */
	mov	$(4 * 512 - 1), %ecx
	/*
	 * XXX: Value below should be 4GB + flags, which wouldn't fit in 32b
	 * register. To avoid warning from the assembler, 4GB is skipped here.
	 * Substitution in first iteration makes the value roll over and point
	 * to 4GB - 2MB + flags.
	 */
	mov	$(_PAGE_PSE + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT), %edx
.Lfill_l2_identmap:
	sub	$(1 << L2_PT_SHIFT), %edx
	/* Loop runs for ecx=[2047..1] for entries [2047..1]. */
	mov	%edx, (%edi,%ecx,8)
	loop	.Lfill_l2_identmap

	/* 1x L3 page, mapping the 4x L2 pages. */
	lea	l3_identmap(%ebp), %edi
	mov	$4, %ecx
	lea	(l2_identmap + 4 * PAGE_SIZE \
	         + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT)(%ebp), %edx
.Lfill_l3_identmap:
	sub	$PAGE_SIZE, %edx
	/* Loop runs for ecx=[4..1] for entries [3..0], hence -8. */
	mov	%edx, -8(%edi,%ecx,8)
	loop	.Lfill_l3_identmap

	/* 1x L4 page, mapping the L3 page. */
	lea	(l3_identmap + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT)(%ebp), %edx
	mov	%edx, l4_identmap(%ebp)

	/* Restore CR4, PAE must be enabled before IA-32e mode */
	mov	%cr4, %ecx
	or	$CR4_PAE, %ecx
	mov	%ecx, %cr4

	/* Load PML4 table location into PT base register */
	lea	l4_identmap(%ebp), %eax
	mov	%eax, %cr3

	/* Enable IA-32e mode and paging */
	mov	$IA32_EFER, %ecx
	rdmsr
	or	$EFER_LME >> 8, %ah
	wrmsr

	mov	%cr0, %eax
	or	$CR0_PG | CR0_NE | CR0_TS | CR0_MP, %eax
	mov	%eax, %cr0

	/* Now in IA-32e compatibility mode, ljmp to 64b mode */
.Ljump64:
        ljmp	$CS_SEL64, $1f /* Offset - dynamically relocated. */

	.code64

1:
#endif	/* 64bit setup. */

	call	skl_main

	/*
	 * skl_main() is magic.  It returns two pointers by register:
	 *
	 * %eax - protected mode kernel entry
	 * %edx - ZP base
	 *
	 * We stash the entry point in %ebx and the Zero Page pointer in %esi.
	 * This both protects them from clobbering during teardown, and
	 * matches the ABI for entering Linux.
	 */
	mov	%eax, %ebx
	mov	%edx, %esi

#ifdef __x86_64__

	/* Setup target to ret to compat mode */
	lea	1f(%rip), %ecx
	push	$CS_SEL32
	push	%rcx
	lretq

	.code32
1:	/* Now in IA-32e compatibility mode, next stop is protected mode */

	/* Turn paging off - we are identity mapped so we will survive */
	mov	%cr0, %eax
	and	$~(CR0_PG | CR0_NE | CR0_TS | CR0_MP), %eax
	mov	%eax, %cr0

	/* Disable IA-32e mode */
	mov	$IA32_EFER, %ecx
	rdmsr
	and	$~(EFER_LME >> 8), %ah
	wrmsr

	/* Now in protected mode, make things look like TXT post launch */
	mov	%cr4, %eax
	and	$~CR4_PAE, %eax
	mov	%eax, %cr4
#endif /* 64bit teardown. */

	push	$0
	popf

	mov	boot_protocol(%ebp), %edx
	cmp	$MULTIBOOT2, %edx
	je	multiboot2_kernel

	cmp	$SIMPLE_PAYLOAD, %edx
	je	simple_payload

	/* Jump to entry target - EBX: startup_32, ESI: ZP base, EDX: SKL base */
	mov	%ebp, %edx
	jmp	*%ebx

multiboot2_kernel:
	/* Jump to entry target - EBX: MBI pointer, EAX: Multiboot2 magic number */
	xchg	%esi, %ebx
	mov	$MULTIBOOT2_BOOTLOADER_MAGIC, %eax
	jmp *%esi

simple_payload:
	/* Jump to entry target - argument and dummy return address on stack */
	push	%esi
	push	$0
	jmp	*%ebx
ENDFUNC(_entry)

	.section .rodata, "a", @progbits
.align 8
gdt:
	/* Null Segment */
	.word	0
	.word	0
	.long	0
	/* 32b Code Segment */
	.word	0xffff /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x9b   /* P=1 DPL=0 S=1 Type=0010 C=0 W=1 A=1 */
	.byte	0xcf   /* G=1 D=1 L=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
	/* Data Segment, can be used both in 32b and 64b */
	.word	0xffff /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x93   /* P=1 DPL=0 S=1 Type=0010 C=0 W=1 A=1 */
	.byte	0xcf   /* G=1 D=1 L=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
#ifdef __x86_64__
	/* 64b Code Segment */
	.word	0x0000 /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x9b   /* P=1 DPL=0 S=1 Type=1010 C=0 R=1 A=1 */
	.byte	0x20   /* G=0 D=0 L=1 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
#endif
.Lgdt_end:
ENDDATA(gdt)

#ifdef __x86_64__
	/* 64bit Pagetables, identity map of the first 4G of RAM. */
	.section .bss.page_data, "aw", @nobits
.align PAGE_SIZE

l1_identmap: /* 1x L1 page, mapping 2M of RAM. */
.skip PAGE_SIZE
ENDDATA(l1_identmap)

l2_identmap: /* 4x L2 pages, each mapping 1G of RAM. */
.skip 4 * PAGE_SIZE
ENDDATA(l2_identmap)

l3_identmap: /* 1x L3 page, mapping the 4x L2 pages. */
.skip PAGE_SIZE
ENDDATA(l3_identmap)

l4_identmap: /* 1x L4 page, mapping the L3 page. */
.skip PAGE_SIZE
ENDDATA(l4_identmap)
#endif

	.section .bootloader_data, "a", @nobits
GLOBAL(bootloader_data)

/* Avoids "missing .note.GNU-stack section implies executable stack" warning. */
	.section .note.GNU-stack

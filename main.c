/*
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

#include <defs.h>
#include <types.h>
#include <boot.h>
#include <pci.h>
#include <iommu.h>
#include "tpmlib/tpm.h"
#include "tpmlib/tpm2_constants.h"
#include <sha1sum.h>
#include <sha256.h>
#include <linux-bootparams.h>
#include <event_log.h>
#include <multiboot2.h>
#include <slrt.h>
#include <string.h>
#include <printk.h>
#include <dev.h>

const skl_info_t __used skl_info = {
    .uuid = {
        0x78, 0xf1, 0x26, 0x8e, 0x04, 0x92, 0x11, 0xe9,
        0x83, 0x2a, 0xc8, 0x5b, 0x76, 0xc4, 0xcc, 0x02,
    },
    .version = 0,
};

static void extend_pcr(struct tpm *tpm, void *data, u32 size, u32 pcr, char *ev)
{
    u8 hash[SHA1_DIGEST_SIZE];
    sha1sum(hash, data, size);
    print("shasum calculated:\n");
    hexdump(hash, SHA1_DIGEST_SIZE);
    tpm_extend_pcr(tpm, pcr, TPM_ALG_SHA1, hash);

    if ( tpm->family == TPM12 )
    {
        log_event_tpm12(pcr, hash, ev);
    }
    else if ( tpm->family == TPM20 )
    {
        u8 sha256_hash[SHA256_DIGEST_SIZE];

        sha256sum(sha256_hash, data, size);
        print("shasum calculated:\n");
        hexdump(sha256_hash, SHA256_DIGEST_SIZE);
        tpm_extend_pcr(tpm, pcr, TPM_ALG_SHA256, &sha256_hash[0]);

        log_event_tpm20(pcr, hash, sha256_hash, ev);
    }

    print("PCR extended\n");
}

/*
 * Checks if ptr points to *uncompressed* part of the kernel
 */
static inline void *is_in_kernel(struct boot_params *bp, void *ptr)
{
    if ( ptr < _p(bp->code32_start) ||
         ptr >= _p(bp->code32_start + (bp->syssize << 4)) ||
         (ptr >= _p(bp->code32_start + bp->payload_offset) &&
          ptr < _p(bp->code32_start + bp->payload_offset + bp->payload_length)) )
        return NULL;
    return ptr;
}

static inline struct kernel_info *get_kernel_info(struct boot_params *bp)
{
    return is_in_kernel(bp, _p(bp->code32_start + bp->kern_info_offset));
}

static inline struct mle_header *get_mle_hdr(struct boot_params *bp,
                                      struct kernel_info *ki)
{
    return is_in_kernel(bp, _p(bp->code32_start + ki->mle_header_offset));
}

static inline void *get_kernel_entry(struct boot_params *bp,
                                     struct mle_header *mle_hdr)
{
    return is_in_kernel(bp, _p(bp->code32_start + mle_hdr->sl_stub_entry));
}

/*
 * Even though die() has both __attribute__((noreturn)) and unreachable(),
 * Clang still complains if it isn't repeated here.
 */
static void __attribute__((noreturn)) reboot(void)
{
    print("Rebooting now...");
    die();
    unreachable();
}

#ifdef TEST_DMA
static void do_dma(void)
{
    /* Set up the DMA channel so we can use it.  This tells the DMA */
    /* that we're going to be using this channel.  (It's masked) */
    outb(0x0a, 0x05);

    /* Clear any data transfers that are currently executing. */
    outb(0x0c, 0x00);

    /* Send the specified mode to the DMA. */
    outb(0x0b, 0x45);

    /* Send the offset address.  The first byte is the low base offset, the */
    /* second byte is the high offset. */
    //~ outportb(AddrPort[DMA_channel], LOW_BYTE(blk->offset));
    //~ outportb(AddrPort[DMA_channel], HI_BYTE(blk->offset));
    outb(0x02, 0x00);
    outb(0x02, 0x00);

    /* Send the physical page that the data lies on. */
    //~ outportb(PagePort[DMA_channel], blk->page);
    outb(0x83, 0x00);

    /* Send the length of the data.  Again, low byte first. */
    //~ outportb(CountPort[DMA_channel], LOW_BYTE(blk->length));
    //~ outportb(CountPort[DMA_channel], HI_BYTE(blk->length));
    outb(0x03, 0x20);
    outb(0x03, 0x00);

    /* Ok, we're done.  Enable the DMA channel (clear the mask). */
    //~ outportb(MaskReg[DMA_channel], DMA_channel);
    outb(0x0a, 0x01);

    // "Device" says that it is ready to send data. As there is no device
    // physically sending the data, this reads idle bus lines.
    outb(0x09, 0x05);
}
#endif

static void iommu_setup(void)
{
    u32 iommu_cap;
    volatile u64 iommu_done __attribute__ ((aligned (8))) = 0;

#ifdef TEST_DMA
    memset(_p(1), 0xcc, 0x20); //_p(0) gives a null-pointer error
    print("before DMA:\n");
    hexdump(_p(0), 0x30);
    do_dma();
    /* Important line, it delays hexdump */
    print("after DMA:              \n");
    hexdump(_p(0), 0x30);
    memset(_p(1), 0xcc, 0x20);
    print("before DMA2\n");
    hexdump(_p(0), 0x30);
    do_dma();
    /* Important line, it delays hexdump */
    print("after DMA2              \n");
    hexdump(_p(0), 0x30);
#endif

    pci_init();
    iommu_cap = iommu_locate();

    /*
     * SKINIT enables protection against DMA access from devices for SLB
     * (whole 64K, not just the measured part). This ensures that no device
     * can overwrite code or data of SL. Unfortunately, it also means that
     * IOMMU, being a PCI device, also cannot read from this memory region.
     * When IOMMU is trying to read a command from buffer located in SLB it
     * receives COMMAND_HARDWARE_ERROR (master abort).
     *
     * Luckily, after that error it enters a fail-safe state in which all
     * operations originating from devices are blocked. The IOMMU itself can
     * still access the memory, so after the SLB protection is lifted, it can
     * try to read the data located inside SLB and set up a proper protection.
     *
     * TODO: split iommu_load_device_table() into two parts, before and after
     *       DEV disabling
     *
     * TODO2: check if IOMMU always blocks the devices, even when it was
     *        configured before SKINIT
     */

    if ( iommu_cap == 0 || iommu_load_device_table(iommu_cap, &iommu_done) )
    {
        if ( iommu_cap )
            print("IOMMU disabled by a firmware, please check your settings\n");

        print("Couldn't set up IOMMU, DMA attacks possible!\n");
    }
    else
    {
        /* Turn off SLB protection, try again */
        print("Disabling SLB protection\n");
        disable_memory_protection();

#ifdef TEST_DMA
        memset(_p(1), 0xcc, 0x20);
        print("before DMA:\n");
        hexdump(_p(0), 0x30);
        do_dma();
        /* Important line, it delays hexdump */
        print("after DMA:              \n");
        hexdump(_p(0), 0x30);
        /* Important line, it delays hexdump */
        print("and again\n");
        hexdump(_p(0), 0x30);

        memset(_p(1), 0xcc, 0x20);
        print("before DMA2\n");
        hexdump(_p(0), 0x30);
        do_dma();
        /* Important line, it delays hexdump */
        print("after DMA2              \n");
        hexdump(_p(0), 0x30);
        /* Important line, it delays hexdump */
        print("and again2\n");
        hexdump(_p(0), 0x30);
#endif

        iommu_load_device_table(iommu_cap, &iommu_done);
        print("Flushing IOMMU cache");
        while ( !iommu_done )
            print(".");

        print("\nIOMMU set\n");
    }

#ifdef TEST_DMA
    memset(_p(1), 0xcc, 0x20);
    print("before DMA:\n");
    hexdump(_p(0), 0x30);
    do_dma();
    /* Important line, it delays hexdump */
    print("after DMA:              \n");
    hexdump(_p(0), 0x30);
    /* Important line, it delays hexdump */
    print("and again\n");
    hexdump(_p(0), 0x30);

    memset(_p(1), 0xcc, 0x20);
    print("before DMA2\n");
    hexdump(_p(0), 0x30);
    do_dma();
    /* Important line, it delays hexdump */
    print("after DMA2              \n");
    hexdump(_p(0), 0x30);
    /* Important line, it delays hexdump */
    print("and again2\n");
    hexdump(_p(0), 0x30);
#endif
}

/*
 * Function return ABI magic:
 *
 * By returning a simple object of two pointers, the SYSV ABI splits it across
 * %rax and %rdx rather than spilling it to the stack.  This is far more
 * convenient for our asm caller to deal with.
 */
typedef struct {
    void *dlme_entry;   /* %eax */
    void *dlme_arg;     /* %edx */
} asm_return_t;

asm_return_t skl_main(void)
{
    struct tpm *tpm;
    struct slr_entry_dl_info *dl_info;
    asm_return_t ret;
    u32 entry_offset;

    /*
     * Now in 64b mode, paging is setup. This is the launching point. We can
     * now do what we want. First order of business is to setup
     * DEV to cover memory from the start of bzImage to the end of the SKL
     * "kernel". At the end, trampoline to the PM entry point which will
     * include the Secure Launch stub.
     */
    pci_init();

    /* Disable memory protection and setup IOMMU */
    iommu_setup();

    /*
     * TODO Note these functions can fail but there is no clear way to
     * report the error unless SKINIT has some resource to do this. For
     * now, if an error is returned, this code will most likely just crash.
     */
    tpm = enable_tpm();
    tpm_request_locality(tpm, 2);
    event_log_init(tpm);

    /*
     * Now that we have TPM and event log, we can begin measuring. For parity
     * with what TXT does, we leave most of measuring for the DLME. Here, we
     * only have to measure DLME itself, as well as entry point offset - only
     * both of those measurements together tell that the proper code has been
     * started. Entry point offset may come from MLE header included in DLME,
     * but we can't trust that the bootloader passed it without modification.
     */
    dl_info = next_entry_with_tag(NULL, SLR_ENTRY_DL_INFO);

    if ( dl_info                                     == NULL
         || dl_info->hdr.size                        != sizeof(*dl_info)
         || end_of_slrt()                             < _p(&dl_info[1])
         || dl_info->dlme_base                       >= 0x100000000ULL
         || dl_info->dlme_base + dl_info->dlme_size  >= 0x100000000ULL
         || dl_info->dlme_entry                      >= dl_info->dlme_size
         || dl_info->bl_context.bootloader           != SLR_BOOTLOADER_GRUB )
    {
        print("Bad bootloader data format\n");
        reboot();
    }

    entry_offset = dl_info->dlme_entry;
    extend_pcr(tpm, &entry_offset, sizeof(entry_offset), 17,
               "DLME entry offset");
    extend_pcr(tpm, _p(dl_info->dlme_base), dl_info->dlme_size, 17, "DLME");

    tpm_relinquish_locality(tpm);
    free_tpm(tpm);

    ret.dlme_entry = _p(dl_info->dlme_base + dl_info->dlme_entry);
    ret.dlme_arg = _p(dl_info->bl_context.context);

    /* End of the line, off to the protected mode entry into the kernel */
    print("dlme_entry:\n");
    hexdump(ret.dlme_entry, 0x100);
    print("dlme_arg:\n");
    hexdump(ret.dlme_arg, 0x280);
    print("skl_base:\n");
    hexdump(_start, 0x100);
    print("bootloader_data:\n");
    hexdump(&bootloader_data, bootloader_data.size);

    print("skl_main() is about to exit\n");

    return ret;
}

static void __maybe_unused build_assertions(void)
{
    BUILD_BUG_ON(offsetof(struct boot_params, tb_dev_map)        != 0x0d8);
    BUILD_BUG_ON(offsetof(struct boot_params, syssize)           != 0x1f4);
    BUILD_BUG_ON(offsetof(struct boot_params, version)           != 0x206);
    BUILD_BUG_ON(offsetof(struct boot_params, code32_start)      != 0x214);
    BUILD_BUG_ON(offsetof(struct boot_params, cmd_line_ptr)      != 0x228);
    BUILD_BUG_ON(offsetof(struct boot_params, cmdline_size)      != 0x238);
    BUILD_BUG_ON(offsetof(struct boot_params, payload_offset)    != 0x248);
    BUILD_BUG_ON(offsetof(struct boot_params, payload_length)    != 0x24c);
    BUILD_BUG_ON(offsetof(struct boot_params, kern_info_offset)  != 0x268);

    BUILD_BUG_ON(offsetof(struct kernel_info, mle_header_offset) != 0x010);
}

#ifndef __SLRT_H__
#define __SLRT_H__

#ifndef __COREBOOT__
#include <defs.h>
#endif /* __COREBOOT__ */
#include <types.h>

/* SLR defined bootloaders */
#define SLR_BOOTLOADER_INVALID  0
#define SLR_BOOTLOADER_GRUB     1

/* Log formats */
#define SLR_DRTM_TPM12_LOG      1
#define SLR_DRTM_TPM20_LOG      2

/* Array Lengths */
#define TPM_EVENT_INFO_LENGTH   32

/* Tags */
#define SLR_ENTRY_INVALID       0x0000
#define SLR_ENTRY_DL_INFO       0x0001
#define SLR_ENTRY_LOG_INFO      0x0002
#define SLR_ENTRY_ENTRY_POLICY  0x0003
#define SLR_ENTRY_INTEL_INFO    0x0004
#define SLR_ENTRY_AMD_INFO      0x0005
#define SLR_ENTRY_ARM_INFO      0x0006
#define SLR_ENTRY_UEFI_INFO     0x0007
#define SLR_ENTRY_UEFI_CONFIG   0x0008
#define SLR_ENTRY_END           0xffff

/*
 * Common SLRT Table Header
 */
struct slr_entry_hdr {
    u32 tag;
    u32 size;
} __packed;

/*
 * Primary Secure Launch Resource Table Header
 */
struct slr_table {
    u32 magic;
    u16 revision;
    u16 architecture;
    u32 size;
    u32 max_size;
    /* Not really a flex array, don't use it that way! */
    struct slr_entry_hdr entries[];
} __packed;

/*
 * Boot loader context
 */
struct slr_bl_context {
    u16 bootloader;
    u16 reserved[3];
    u64 context;
} __packed;

/*
 * DRTM Dynamic Launch Configuration
 */
struct slr_entry_dl_info {
    struct slr_entry_hdr hdr;
    u64 dce_size;
    u64 dce_base;
    u64 dlme_size;
    u64 dlme_base;
    u64 dlme_entry; /* Offset from dlme_base */
    struct slr_bl_context bl_context;
    u64 dl_handler;
} __packed;

/*
 * TPM Log Information
 */
struct slr_entry_log_info {
    struct slr_entry_hdr hdr;
    u16 format;
    u16 reserved;
    u32 size;
    u64 addr;
} __packed;

/*
 * AMD SKINIT Info table
 */
struct slr_entry_amd_info {
    struct slr_entry_hdr hdr;
    u64 next;
    u32 type;
    u32 len;
    u64 slrt_size;
    u64 slrt_base;
    u64 boot_params_base;
    u16 psp_version;
    u16 reserved[3];
} __packed;

#ifndef __COREBOOT__
/* Secure Kernel Loader */
extern struct slr_table bootloader_data;

static inline void *end_of_slrt(void)
{
    return _p(_u(&bootloader_data) + bootloader_data.size);
}

static inline void *next_entry(void* t)
{
    void *x = t + ((struct slr_entry_hdr*)t)->size;
    return x < end_of_slrt() ? x : NULL;
}

static inline void *next_entry_with_tag(void* _t, u16 tag)
{
    struct slr_entry_hdr *t = _t;
    if (t == NULL) {
        t = &bootloader_data.entries[0];
        if ( t->tag == tag )
            return (void*)t < end_of_slrt() ? t : NULL;
    }

    while ( t->tag != SLR_ENTRY_END )
    {
        t = next_entry(t);
        if ( t->tag == tag )
            return (void*)t < end_of_slrt() ? t : NULL;
    }
    return NULL;
}
#endif /* __COREBOOT__ */

#endif /* __SLRT_H__ */

#ifndef __SLRT_H__
#define __SLRT_H__

#include <defs.h>
#include <types.h>

struct slr_entry_hdr {
    u16 tag;
    u16 size;
} __packed;

#define SLR_ENTRY_INVALID           0x0000
#define SLR_ENTRY_DL_INFO           0x0001
#define SLR_ENTRY_LOG_INFO          0x0002
#define SLR_ENTRY_DRTM_POLICY       0x0003
#define SLR_ENTRY_INTEL_INFO        0x0004
#define SLR_ENTRY_AMD_INFO          0x0005
#define SLR_ENTRY_ARM_INFO          0x0006
#define SLR_ENTRY_UEFI_INFO         0x0007
#define SLR_ENTRY_UEFI_CONFIG       0x0008
#define SLR_ENTRY_END               0xffff

struct slr_table {
    u32 magic;
    u16 revision;
    u16 architecture;
    u32 size;
    u32 max_size;
    /* Not really a flex array, don't use it that way! */
    struct slr_entry_hdr entries[];
} __packed;

struct slr_bl_context {
    u16 bootloader;
    u16 reserved;
    u64 context;
} __packed;

#define SLR_BOOTLOADER_GRUB         1

struct slr_entry_dl_info {
    struct slr_entry_hdr hdr;
    struct slr_bl_context bl_context;
    u64 dl_handler;
    u64 dce_base;
    u32 dce_size;
    u64 dlme_base;
    u32 dlme_size;
    u32 dlme_entry;     /* Offset from dlme_base */
} __packed;

struct slr_entry_log_info {
    struct slr_entry_hdr hdr;
    u16 format;
    u16 reserved;
    u64 addr;
    u32 size;
} __packed;

#define SLR_LOG_FORMAT_TPM12_TXT     1
#define SLR_LOG_FORMAT_TPM20_TCG     2

#define TPM_EVENT_INFO_LENGTH       20

struct slr_policy_entry {
    u16 pcr;
    u16 entity_type;
    u16 flags;
    u16 reserved;
    u64 entity;
    u64 size;
    char evt_info[TPM_EVENT_INFO_LENGTH];
} __packed;

/* Constants for entity_type */
#define SLR_ET_UNSPECIFIED          0x0000
#define SLR_ET_SLRT                 0x0001
#define SLR_ET_LINUX_BOOT_PARAMS    0x0002
#define SLR_ET_LINUX_SETUP_DATA     0x0003
#define SLR_ET_CMDLINE              0x0004
#define SLR_ET_UEFI_MEMMAP          0x0005
#define SLR_ET_RAMDISK              0x0006
#define SLR_ET_MULTIBOOT2_INFO      0x0007
#define SLR_ET_MULTIBOOT2_MODULE    0x0008
// values 0x0009-0x000f reserved for future use
// TXT-specific:
#define SLR_ET_TXT_OS2MLE           0x0010
#define SLR_ET_UNUSED               0xffff

/* Constants for flags */
#define SLR_POLICY_FLAG_MEASURED    0x1
#define SLR_POLICY_IMPLICIT_SIZE    0x2

struct slr_entry_policy {
    struct slr_entry_hdr hdr;
    u16 revision;
    u16 nr_entries;
    struct slr_policy_entry policy_entries[];
} __packed;

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

#endif /* __SLRT_H__ */

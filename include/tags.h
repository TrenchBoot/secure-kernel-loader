#ifndef __TAGS_H__
#define __TAGS_H__

#include <defs.h>
#include <types.h>

/* extensible setup indirect data node */
struct setup_indirect {
    u32 type;
    u32 reserved;  /* Reserved, must be set to zero. */
    u64 len;
    u64 addr;
} __packed;

/* extensible setup data list node */
struct setup_data {
    u64 next;
    u32 type;
    u32 len;
    struct setup_indirect indirect;
} __packed;

#define SKL_TAG_CLASS_MASK       0xF0

/* Tags with no particular class */
#define SKL_TAG_NO_CLASS         0x00
#define SKL_TAG_END              0x00
#define SKL_TAG_SETUP_INDIRECT   0x01
#define SKL_TAG_TAGS_SIZE        0x0F    /* Always first */

/* Tags specifying kernel type */
#define SKL_TAG_BOOT_CLASS       0x10
#define SKL_TAG_BOOT_LINUX       0x10
#define SKL_TAG_BOOT_MB2         0x11
#define SKL_TAG_BOOT_SIMPLE      0x12

/* Tags specific to TPM event log */
#define SKL_TAG_EVENT_LOG_CLASS  0x20
#define SKL_TAG_EVENT_LOG        0x20
#define SKL_TAG_SKL_HASH         0x21

struct skl_tag_hdr {
    u8 type;
    u8 len;
} __packed;

struct skl_tag_tags_size {
    struct skl_tag_hdr hdr;
    u16 size;
} __packed;

struct skl_tag_boot_linux {
    struct skl_tag_hdr hdr;
    u32 zero_page;
} __packed;

struct skl_tag_boot_mb2 {
    struct skl_tag_hdr hdr;
    u32 mbi;
    u32 kernel_entry;
    u32 kernel_size;
} __packed;

struct skl_tag_boot_simple_payload {
    struct skl_tag_hdr hdr;
    u32 base;
    u32 size;
    u32 entry;
    u32 arg;
} __packed;

struct skl_tag_evtlog {
    struct skl_tag_hdr hdr;
    u32 address;
    u32 size;
} __packed;

struct skl_tag_hash {
    struct skl_tag_hdr hdr;
    u16 algo_id;
    u8 digest[];
} __packed;

struct skl_tag_setup_indirect {
    struct skl_tag_hdr hdr;
    struct setup_data data;
} __packed;

extern struct skl_tag_tags_size bootloader_data;

static inline void *end_of_tags(void)
{
    return (((void *) &bootloader_data) + bootloader_data.size);
}

static inline void *next_tag(void* t)
{
    void *x = t + ((struct skl_tag_hdr*)t)->len;
    return x < end_of_tags() ? x : NULL;
}

static inline void *next_of_type(void* _t, u8 type)
{
    struct skl_tag_hdr *t = _t;
    while ( t->type != SKL_TAG_END )
    {
        t = next_tag(t);
        if ( t->type == type )
            return (void*)t < end_of_tags() ? t : NULL;
    }
    return NULL;
}

static inline void *next_of_class(void* _t, u8 c)
{
    struct skl_tag_hdr *t = _t;
    while ( t->type != SKL_TAG_END )
    {
        t = next_tag(t);
        if ( (t->type & SKL_TAG_CLASS_MASK) == c )
            return (void*)t < end_of_tags() ? t : NULL;
    }
    return NULL;
}

#endif /* __TAGS_H__ */

/*
 * Copyright (C) 2020 3mdeb Embedded Systems Consulting
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

#include <boot.h>
#include <printk.h>
#include <sha1sum.h>
#include <sha256.h>
#include <slrt.h>
#include <string.h>
#include "tpmlib/tpm.h"
#include "tpmlib/tpm_common.h"
#include "tpmlib/tpm2_constants.h"

static u8 *evtlog_base;
static u8 *ptr_current;
static u8 *limit;

#define HAS_ENOUGH_SPACE(n)      ((limit - ptr_current) > (n))

static int log_write(const void *data, unsigned size)
{
    if ( size >= limit - ptr_current )
        return 1;

    memcpy(ptr_current, data, size);
    ptr_current += size;
    return 0;
}

#define EV_NO_ACTION    0x3
#define EV_TYPE_SLAUNCH 0x502

/*
 * One bank per algorithm the SKL knows the digest size of: SHA-1, SHA-256,
 * SHA-384, SHA-512 and SM3-256.
 */
#define MAX_BANKS 5

/*
 * The TPM's allocated banks in its order, which the TPM 2.0 log declares
 * and every record of it carries. Where the SKL cannot compute a digest,
 * the record carries 01 followed by zeros, as Linux and Xen log for such
 * a bank. The Intel TXT Software Development Guide calls it OneDigest.
 */
static struct __packed {
    u16 alg;
    u16 size;
} banks[MAX_BANKS];
static u32 nbanks;

static const u8 one_digest[SHA512_SIZE] = { 1 };

static u16 digest_size(u16 alg)
{
    switch ( alg )
    {
    case TPM_ALG_SHA1:    return SHA1_SIZE;
    case TPM_ALG_SHA256:  return SHA256_SIZE;
    case TPM_ALG_SHA384:  return SHA384_SIZE;
    case TPM_ALG_SHA512:  return SHA512_SIZE;
    case TPM_ALG_SM3_256: return SM3256_SIZE;
    default:              return 0;
    }
}

/* For compatibility with TXT and easier operations */

#define TPM12_EVTLOG_SIGNATURE "TXT Event Container"

typedef struct __packed {
    char signature[20];
    char reserved[12];
    u8 container_ver_major;
    u8 container_ver_minor;
    u8 pcr_event_ver_major;
    u8 pcr_event_ver_minor;
    u32 container_size;
    u32 pcr_events_offset;
    u32 next_event_offset;
    /* PCREvents[] */
} tpm12_event_log_header;

typedef struct __packed {
    u64 phys_addr;
    u32 allocated_event_container_size;
    u32 first_record_offset;
    u32 next_record_offset;
} txt_event_log_pointer2_1_element;

/* Event log entries */

typedef struct __packed {
    u32 pcr;
    u32 event_type;
    u8  digest[SHA1_DIGEST_SIZE];
    u32 event_size;
    /* u8 event[]; */
} tpm12_event_t;

/* Event log headers */

typedef struct __packed {
    char signature[16];
    u32  platform_class;
    u8   spec_ver_minor;
    u8   spec_ver_major;
    u8   errata;
    u8   uintn_size;        /* reserved (must be 0) for 1.21 */
} common_spec_id_ev_t;

typedef struct __packed {
    common_spec_id_ev_t c;
    u8   vendor_info_size;
    tpm12_event_log_header hdr;             /* AKA u8 vendor_info[]; */
} tpm12_spec_id_ev_t;

/*
 * The TPM 2.0 header is common_spec_id_ev_t, the number of algorithms,
 * an id and size pair per bank, and the vendor info with its size. Its
 * size depends on the number of banks.
 */
static u32 tpm20_header_size(void)
{
    return sizeof(common_spec_id_ev_t) + sizeof(u32) + nbanks * sizeof(banks[0])
           + 1 + sizeof(txt_event_log_pointer2_1_element);
}

/* The vendor info of the TPM 2.0 header in the log */
static txt_event_log_pointer2_1_element *tpm20_el(void)
{
    return (txt_event_log_pointer2_1_element *)
           (evtlog_base + sizeof(tpm12_event_t) + tpm20_header_size()
            - sizeof(txt_event_log_pointer2_1_element));
}

/*
 * A TPM 2.0 record is the PCR, the type, the digest count, an id and
 * digest per bank, the event size and the event: TCG_PCR_EVENT2.
 */
static u32 tpm20_record_size(u32 event_size)
{
    u32 i, size = 3 * sizeof(u32) + sizeof(u32) + event_size;

    for ( i = 0; i < nbanks; i++ )
        size += sizeof(u16) + banks[i].size;

    return size;
}

static const tpm12_spec_id_ev_t tpm12_id_struct = {
    .c.signature = "Spec ID Event00",
    .c.spec_ver_minor = 2,
    .c.spec_ver_major = 1,
    .c.errata = 1,
    .vendor_info_size = sizeof(tpm12_event_log_header),
    .hdr.signature = TPM12_EVTLOG_SIGNATURE,
    .hdr.container_ver_major = 1,
    .hdr.container_ver_minor = 0,
    .hdr.pcr_event_ver_major = 1,
    .hdr.pcr_event_ver_minor = 0,
    /*
     * HACK: this offset should be relative to the base of Event Log, but TXT
     * creates its log starting with the .hdr.signature, not .c.signature.
     * Linux kernel sets its evtlog_base to the address of the former one in
     * order to use the same code for both of the supported CPU vendors.
     */
    .hdr.pcr_events_offset = sizeof(tpm12_event_log_header),
    .hdr.next_event_offset = sizeof(tpm12_event_log_header)
};

static const common_spec_id_ev_t tpm20_id_common = {
    .signature = "Spec ID Event03",
    .spec_ver_minor = 0,
    .spec_ver_major = 2,
    .errata = 0,
    .uintn_size = 2,
};

int log_event_tpm12(u32 pcr, u8 sha1[SHA1_DIGEST_SIZE], char *event)
{
    tpm12_event_t ev;
    tpm12_spec_id_ev_t *base = (tpm12_spec_id_ev_t *)
                                (evtlog_base + sizeof(tpm12_event_t));

    ev.event_size = strlen(event);

    if ( HAS_ENOUGH_SPACE(sizeof(ev) + ev.event_size) )
    {
        ev.pcr = pcr;
        ev.event_type = EV_TYPE_SLAUNCH;
        memcpy(ev.digest, sha1, 20);
        base->hdr.next_event_offset += sizeof(ev) + ev.event_size;
        log_write(&ev, sizeof(ev));
        return log_write(event, ev.event_size);
    }

    return 1;
}

/*
 * Writes a TPM 2.0 record with digests[i] in bank i, or the placeholder
 * where that is NULL. Returns non-zero when it does not fit.
 */
static int write_event_tpm20(u32 pcr, u32 type, const u8 *const digests[],
                             const void *event, u32 event_size)
{
    u32 i, head[3] = { pcr, type, nbanks };

    if ( !HAS_ENOUGH_SPACE(tpm20_record_size(event_size)) )
        return 1;

    log_write(head, sizeof(head));
    for ( i = 0; i < nbanks; i++ )
    {
        log_write(&banks[i].alg, sizeof(banks[i].alg));
        log_write(digests[i] ? digests[i] : one_digest, banks[i].size);
    }
    log_write(&event_size, sizeof(event_size));
    log_write(event, event_size);
    tpm20_el()->next_record_offset += tpm20_record_size(event_size);

    return 0;
}

int log_event_tpm20(u32 pcr, u8 sha1[SHA1_DIGEST_SIZE],
                    u8 sha256[SHA256_DIGEST_SIZE], char *event)
{
    const u8 *digests[MAX_BANKS] = { NULL };
    u32 i;

    for ( i = 0; i < nbanks; i++ )
        digests[i] = banks[i].alg == TPM_ALG_SHA1 ? sha1
                     : banks[i].alg == TPM_ALG_SHA256 ? sha256 : NULL;

    return write_event_tpm20(pcr, EV_TYPE_SLAUNCH, digests, event,
                             strlen(event));
}

void event_log_extend_placeholders(struct tpm *tpm, u32 pcr)
{
    u32 i;

    for ( i = 0; i < nbanks; i++ )
        if ( banks[i].alg != TPM_ALG_SHA1 && banks[i].alg != TPM_ALG_SHA256 )
            tpm_extend_pcr(tpm, pcr, banks[i].alg, (u8 *)one_digest);
}

/*
 * Asks the TPM 2.0 for its allocated banks, leaving out algorithms the SKL
 * does not know. If the query fails, or finds no bank the SKL knows, the
 * log declares SHA-1 and SHA-256, the banks most discrete TPMs have
 * allocated by default. The query fails behind a locality the SKL does
 * not hold, and on every CRB TPM, because crb_recv() does not read the
 * reply.
 */
static void find_banks(struct tpm *tpm)
{
    u16 algs[MAX_BANKS];
    u32 n = MAX_BANKS, i;
    int rc;

    nbanks = 0;
    rc = tpm_get_pcr_banks(tpm, algs, &n);
    if ( rc == 0 )
    {
        for ( i = 0; i < n; i++ )
        {
            if ( digest_size(algs[i]) == 0 )
                continue;
            banks[nbanks].alg = algs[i];
            banks[nbanks].size = digest_size(algs[i]);
            nbanks++;
        }
    }
    else
    {
        print("Bank query failed: ");
        print_u64((u32)rc);
        print("\n");
    }

    if ( nbanks == 0 )
    {
        banks[0].alg = TPM_ALG_SHA1;
        banks[0].size = SHA1_SIZE;
        banks[1].alg = TPM_ALG_SHA256;
        banks[1].size = SHA256_SIZE;
        nbanks = 2;
    }

    print("Event log banks:");
    for ( i = 0; i < nbanks; i++ )
    {
        print(" ");
        print_u64(banks[i].alg);
    }
    print("\n");
}

int event_log_init(struct tpm *tpm)
{
    unsigned int min_size;
    struct slr_entry_log_info *info;
    u8 hash[SHA1_DIGEST_SIZE];

    info = next_entry_with_tag(NULL, SLR_ENTRY_LOG_INFO);

    if ( info == NULL || next_entry_with_tag(info, SLR_ENTRY_LOG_INFO) != NULL )
        goto err;

    min_size = sizeof (tpm12_event_t);

    if ( tpm->family == TPM12 )
    {
        min_size += sizeof(tpm12_id_struct);
        min_size += 2 * sizeof(tpm12_event_t); /* SKL and kernel hashes */
    }
    else if ( tpm->family == TPM20 )
    {
        find_banks(tpm);
        min_size += tpm20_header_size();
        min_size += 2 * tpm20_record_size(0); /* SKL and kernel hashes */
    }
    else
    {
        goto err;
    }

    /* Note that min_size does not include tpmXX_event_t.event[] entries */
    if ( info->size < min_size )
        goto err;

    ptr_current = evtlog_base = _p(info->addr);
    limit = _p(info->addr + info->size);

    /* Check for overflow */
    if ( ptr_current > limit )
        goto err;

    /*
     * Bootloader controls location and size, so it could force SKL to overwrite
     * its code **after** it was measured. Make sure that the Event Log and SKL
     * do not overlap before wiping the memory.
     */
    if ( !(_p(limit) < _p(_start) || _p(_start + SLB_SIZE) < _p(ptr_current)) )
        goto err;

    memset(ptr_current, 0, info->size);

    /* Check if log format matches TPM family */
    if ((tpm->family == TPM12 && info->format != SLR_DRTM_TPM12_LOG) ||
        (tpm->family == TPM20 && info->format != SLR_DRTM_TPM20_LOG))
        goto err;

    /* Write log header */
    {
        tpm12_event_t ev;

        ev.pcr = 0;
        ev.event_type = EV_NO_ACTION;
        memset(ev.digest, 0, 20);
        if ( tpm->family == TPM12 )
            ev.event_size = sizeof(tpm12_id_struct);
        else
            ev.event_size = tpm20_header_size();

        log_write(&ev, sizeof(ev));
    }

    /* Sizes were checked earlier so log_write() won't fail here */
    if ( tpm->family == TPM12 ) {
        tpm12_spec_id_ev_t *id = (tpm12_spec_id_ev_t *)ptr_current;
        log_write(&tpm12_id_struct, sizeof(tpm12_id_struct));
        id->hdr.container_size = info->size;
    } else {
        u8 vendor_info_size = sizeof(txt_event_log_pointer2_1_element);
        txt_event_log_pointer2_1_element el = {
            .phys_addr = _u(evtlog_base),
            .allocated_event_container_size = info->size,
            .first_record_offset = 0,
            .next_record_offset = sizeof(tpm12_event_t) + tpm20_header_size(),
        };

        log_write(&tpm20_id_common, sizeof(tpm20_id_common));
        log_write(&nbanks, sizeof(nbanks));
        log_write(banks, nbanks * sizeof(banks[0]));
        log_write(&vendor_info_size, sizeof(vendor_info_size));
        log_write(&el, sizeof(el));
    }

    /* Log what was done by SKINIT */
    sha1sum(hash, _start, _end_of_measured - _start);
    if ( tpm->family == TPM12 )
    {
        return log_event_tpm12(17, hash, "SKINIT");
    }
    else if ( tpm->family == TPM20 )
    {
        u8 sha256_hash[SHA256_DIGEST_SIZE];

        sha256sum(sha256_hash, _start, _end_of_measured - _start);
        /*
         * SKINIT extended PCR 17 with the real digest in every bank, so
         * this record does not replay in a bank that gets the placeholder.
         */
        return log_event_tpm20(17, hash, sha256_hash, "SKINIT");
    }

err:
    /* Make sure that further calls to log_write() will fail */
    limit = ptr_current;
    return 1;
}

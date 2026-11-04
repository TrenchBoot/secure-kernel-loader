/*
 * A hosted test of the TPM 2.0 event log: the header and records follow
 * the TPM's banks, with the placeholder where the SKL has no digest.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

/* The host's C runtime owns _start, so the SKL's symbols go by other names */
#define _start skl_start
#define _end_of_measured skl_end_of_measured

#include "sha1sum.c"
#include "sha256.c"
#include "event_log.c"

#ifdef DEBUG
void print(const char *unused) { }
void print_u64(u64 unused) { }
#endif

/* An SLB-sized image whose first 16 bytes are what SKINIT measured */
const char skl_start[SLB_SIZE] = "SKINIT measured";
__asm__(".globl skl_end_of_measured\n.set skl_end_of_measured, skl_start + 16");

/*
 * The SLRT that gives the SKL its log region: one log info entry, then
 * the end tag. Its flexible array is initialised with room for both.
 */
#define SLRT_ENTRIES \
    ((sizeof(struct slr_entry_log_info) + sizeof(struct slr_entry_hdr)) / \
     sizeof(struct slr_entry_hdr))

struct slr_table bootloader_data = {
    .entries = { [SLRT_ENTRIES - 1] = { 0 } },
};
static struct slr_entry_log_info *const slrt_log =
    (struct slr_entry_log_info *)bootloader_data.entries;
static struct slr_entry_hdr *const slrt_end =
    (struct slr_entry_hdr *)bootloader_data.entries + SLRT_ENTRIES - 1;

static u8 log_region[4096];

/* What the TPM answers for its banks */
static u16 tpm_algs[MAX_BANKS];
static u32 tpm_nalgs;
static int tpm_banks_rc;

int tpm_get_pcr_banks(struct tpm *t, u16 *algs, u32 *count)
{
    if ( tpm_banks_rc )
        return tpm_banks_rc;
    memcpy(algs, tpm_algs, tpm_nalgs * sizeof(u16));
    *count = tpm_nalgs;
    return 0;
}

static void tpm_has(u32 n, ...)
{
    va_list ap;
    u32 i;

    va_start(ap, n);
    for ( i = 0; i < n; i++ )
        tpm_algs[i] = va_arg(ap, int);
    va_end(ap);
    tpm_nalgs = n;
    tpm_banks_rc = 0;
}

/* The extends made: (pcr, alg, whether the placeholder) */
static struct {
    u32 pcr;
    u16 alg;
    bool placeholder;
} extends[32];
static unsigned nextends;

static bool is_placeholder(const u8 *digest, u32 size)
{
    u32 i;

    if ( digest[0] != 1 )
        return false;
    for ( i = 1; i < size; i++ )
        if ( digest[i] != 0 )
            return false;
    return true;
}

int tpm_extend_pcr(struct tpm *t, u32 pcr, u16 algo, u8 *digest)
{
    if ( nextends < 32 )
    {
        extends[nextends].pcr = pcr;
        extends[nextends].alg = algo;
        extends[nextends].placeholder =
            is_placeholder(digest, digest_size(algo));
    }
    nextends++;
    return 0;
}

static struct tpm tpm20 = { .family = TPM20 };

static int failures;

#define CHECK(cond) do {                                             \
    if ( !(cond) ) {                                                 \
        printf("%s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        failures++;                                                  \
    }                                                                \
} while ( 0 )

static void fresh_log(u32 size)
{
    nextends = 0;
    bootloader_data.size = sizeof(bootloader_data) +
                           SLRT_ENTRIES * sizeof(struct slr_entry_hdr);
    slrt_log->hdr.tag = SLR_ENTRY_LOG_INFO;
    slrt_log->hdr.size = sizeof(*slrt_log);
    slrt_log->format = SLR_DRTM_TPM20_LOG;
    slrt_log->size = size;
    slrt_log->addr = _u(log_region);
    slrt_end->tag = SLR_ENTRY_END;
    slrt_end->size = sizeof(*slrt_end);
    memset(log_region, 0xa5, sizeof(log_region));
    CHECK(event_log_init(&tpm20) == 0);
}

/* The header as a reader parses it: the algorithm table and the vendor info */
static struct {
    u32 nalgs;
    u16 alg[MAX_BANKS];
    u16 size[MAX_BANKS];
    txt_event_log_pointer2_1_element *el;
    const u8 *records;
} hdr;

static void parse_header(void)
{
    const u8 *p = log_region + sizeof(tpm12_event_t);
    u32 i;

    CHECK(memcmp(p, "Spec ID Event03", 16) == 0);
    p += sizeof(common_spec_id_ev_t);
    memcpy(&hdr.nalgs, p, 4);
    p += 4;
    CHECK(hdr.nalgs >= 1 && hdr.nalgs <= MAX_BANKS);
    for ( i = 0; i < hdr.nalgs && i < MAX_BANKS; i++ )
    {
        memcpy(&hdr.alg[i], p, 2);
        memcpy(&hdr.size[i], p + 2, 2);
        p += 4;
    }
    CHECK(*p == sizeof(*hdr.el));
    hdr.el = (txt_event_log_pointer2_1_element *)(p + 1);
    hdr.records = p + 1 + sizeof(*hdr.el);
}

/*
 * The records after the header, parsed as a reader would: each carries
 * the header's banks in its order, and for every bank the first digest
 * byte and whether it is the placeholder.
 */
struct record {
    u32 pcr, type;
    u8 first[MAX_BANKS];
    bool placeholder[MAX_BANKS];
};

static unsigned records(struct record *out, unsigned max)
{
    const u8 *p;
    unsigned n = 0;

    parse_header();
    p = hdr.records;
    while ( n < max )
    {
        u32 count, i, len;

        memcpy(&out[n].pcr, p, 4);
        memcpy(&out[n].type, p + 4, 4);
        memcpy(&count, p + 8, 4);
        if ( count == 0 )
            break;
        p += 12;
        CHECK(count == hdr.nalgs);
        for ( i = 0; i < count && i < MAX_BANKS; i++ )
        {
            u16 alg;
            memcpy(&alg, p, 2);
            CHECK(alg == hdr.alg[i]);
            out[n].first[i] = p[2];
            out[n].placeholder[i] = is_placeholder(p + 2, hdr.size[i]);
            p += 2 + hdr.size[i];
        }
        memcpy(&len, p, 4);
        p += 4 + len;
        n++;
    }
    return n;
}

int main(void)
{
    u8 skinit_sha1[20], skinit_sha256[32];
    struct record recs[16];
    unsigned n;

    sha1sum(skinit_sha1, skl_start, 16);
    sha256sum(skinit_sha256, skl_start, 16);

    /* A TPM with the two banks a discrete one ships with */
    tpm_has(2, TPM_ALG_SHA1, TPM_ALG_SHA256);
    fresh_log(sizeof(log_region));
    n = records(recs, 16);
    CHECK(hdr.nalgs == 2 && hdr.alg[0] == TPM_ALG_SHA1 && hdr.size[0] == 20);
    CHECK(hdr.alg[1] == TPM_ALG_SHA256 && hdr.size[1] == 32);
    CHECK(hdr.el->phys_addr == _u(log_region));
    CHECK(hdr.el->allocated_event_container_size == sizeof(log_region));
    CHECK(hdr.el->first_record_offset == 0);
    /* The header event, the header with its two banks, then SKINIT's record */
    CHECK(hdr.records == log_region + 32 + 24 + 4 + 8 + 1 + 20);
    CHECK(hdr.el->next_record_offset == 32 + 24 + 4 + 8 + 1 + 20 + 72 + 6);
    CHECK(n == 1);
    CHECK(recs[0].pcr == 17 && recs[0].type == EV_TYPE_SLAUNCH);
    CHECK(recs[0].first[0] == skinit_sha1[0] && !recs[0].placeholder[0]);
    CHECK(recs[0].first[1] == skinit_sha256[0] && !recs[0].placeholder[1]);
    /* Nothing to extend the placeholder into */
    event_log_extend_placeholders(&tpm20, 17);
    CHECK(nextends == 0);

    /* A TPM with only SHA-256: one digest per record and no placeholder */
    tpm_has(1, TPM_ALG_SHA256);
    fresh_log(sizeof(log_region));
    n = records(recs, 16);
    CHECK(hdr.nalgs == 1 && hdr.alg[0] == TPM_ALG_SHA256);
    CHECK(hdr.records == log_region + 32 + 24 + 4 + 4 + 1 + 20);
    CHECK(n == 1 && recs[0].first[0] == skinit_sha256[0]);
    CHECK(!recs[0].placeholder[0]);
    event_log_extend_placeholders(&tpm20, 17);
    CHECK(nextends == 0);

    /*
     * A TPM with SHA-384 as well, in its own order: the SKL's records get
     * the placeholder there, which is extended.
     */
    tpm_has(3, TPM_ALG_SHA256, TPM_ALG_SHA1, TPM_ALG_SHA384);
    fresh_log(sizeof(log_region));
    n = records(recs, 16);
    CHECK(hdr.nalgs == 3 && hdr.alg[0] == TPM_ALG_SHA256);
    CHECK(hdr.alg[1] == TPM_ALG_SHA1);
    CHECK(hdr.alg[2] == TPM_ALG_SHA384 && hdr.size[2] == 48);
    CHECK(n == 1);
    CHECK(recs[0].first[0] == skinit_sha256[0] && !recs[0].placeholder[0]);
    CHECK(recs[0].first[1] == skinit_sha1[0] && !recs[0].placeholder[1]);
    CHECK(recs[0].placeholder[2]);
    event_log_extend_placeholders(&tpm20, 17);
    CHECK(nextends == 1);
    CHECK(extends[0].pcr == 17 && extends[0].alg == TPM_ALG_SHA384);
    CHECK(extends[0].placeholder);

    /* A bank of an algorithm the SKL cannot size is left out */
    tpm_has(2, 0x99, TPM_ALG_SHA256);
    fresh_log(sizeof(log_region));
    records(recs, 16);
    CHECK(hdr.nalgs == 1 && hdr.alg[0] == TPM_ALG_SHA256);

    /* When the TPM does not answer, the log declares SHA-1 and SHA-256 */
    tpm_banks_rc = -1;
    fresh_log(sizeof(log_region));
    records(recs, 16);
    CHECK(hdr.nalgs == 2 && hdr.alg[0] == TPM_ALG_SHA1);
    CHECK(hdr.alg[1] == TPM_ALG_SHA256);

    /* A region too small for the header and two bare records is refused */
    tpm_has(2, TPM_ALG_SHA1, TPM_ALG_SHA256);
    nextends = 0;
    slrt_log->size = 32 + 57 + 2 * 72 - 1;
    CHECK(event_log_init(&tpm20) != 0);
    slrt_log->size = 32 + 57 + 2 * 72;
    CHECK(event_log_init(&tpm20) == 0);

    if ( failures )
    {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}

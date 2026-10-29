/*
 * Copyright (c) 2019 Apertus Solutions, LLC
 *
 * Author(s):
 *      Daniel P. Smith <dpsmith@apertussolutions.com>
 *
 */

#ifdef LINUX_KERNEL

#include <linux/types.h>
#include <linux/const.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#elif defined LINUX_USERSPACE

#include <string.h>
#include <endian.h>
#include <errno.h>

#define cpu_to_be16 htobe16
#define cpu_to_be32 htobe32

#endif

#include <string.h>

#include "tpm.h"
#include "tpmbuff.h"
#include "tpm_common.h"
#include "tpm2.h"
#include "tpm2_auth.h"
#include "tis.h"
#include "crb.h"

static int tpm2_alloc_cmd(struct tpmbuff *b, struct tpm2_cmd *c, u16 tag,
		u32 code)
{
	/* ensure buffer is free for use */
	tpmb_free(b);

	c->header = (struct tpm_header *)tpmb_reserve(b);
	if (!c->header)
		return -ENOMEM;

	c->header->tag = cpu_to_be16(tag);
	c->header->code = cpu_to_be32(code);

	return 0;
}

static u16 convert_digest_list(struct tpml_digest_values *digests)
{
	int i;
	u16 size = sizeof(digests->count);
	struct tpmt_ha *h = digests->digests;

	for (i = 0; i < digests->count; i++) {
		switch (h->alg) {
		case TPM_ALG_SHA1:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((u8 *)h + SHA1_SIZE);
			size += sizeof(u16) + SHA1_SIZE;
			break;
		case TPM_ALG_SHA256:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((u8 *)h + SHA256_SIZE);
			size += sizeof(u16) + SHA256_SIZE;
			break;
		case TPM_ALG_SHA384:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((u8 *)h + SHA384_SIZE);
			size += sizeof(u16) + SHA384_SIZE;
			break;
		case TPM_ALG_SHA512:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((u8 *)h + SHA512_SIZE);
			size += sizeof(u16) + SHA512_SIZE;
			break;
		case TPM_ALG_SM3_256:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((u8 *)h + SM3256_SIZE);
			size += sizeof(u16) + SM3256_SIZE;
			break;
		default:
			return 0;
		}
	}

	digests->count = cpu_to_be32(digests->count);

	return size;
}

int tpm2_extend_pcr(struct tpm *t, u32 pcr,
		struct tpml_digest_values *digests)
{
	struct tpmbuff *b = t->buff;
	struct tpm2_cmd cmd;
	u16 size;
	int ret = 0;

	if (b == NULL) {
		ret = -EINVAL;
		goto out;
	}

	ret = tpm2_alloc_cmd(b, &cmd, TPM_ST_SESSIONS, TPM_CC_PCR_EXTEND);
	if (ret < 0)
		goto out;

	cmd.handles = (u32 *)tpmb_put(b, sizeof(u32));
	if (cmd.handles == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	cmd.handles[0] = cpu_to_be32(pcr);

	cmd.auth_size = (u32 *)tpmb_put(b, sizeof(u32));
	if (cmd.auth_size == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	cmd.auth = tpm2_null_auth(b);
	if (cmd.auth == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	*cmd.auth_size = cpu_to_be32(tpm2_null_auth_size());

	size = convert_digest_list(digests);
	if (size == 0) {
		ret = -ENOMEM;
		goto free;
	}

	cmd.params = (u8 *)tpmb_put(b, size);
	if (cmd.params == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	memcpy(cmd.params, digests, size);

	cmd.header->size = cpu_to_be32(tpmb_size(b));

	size = t->ops.send(b);
	if (tpmb_size(b) != size)
		ret = -EAGAIN;

free:
	tpmb_free(b);
out:
	return ret;
}

int tpm2_get_pcr_banks(struct tpm *t, u16 *algs, u32 *count)
{
	struct tpmbuff *b = t->buff;
	struct tpm2_cmd cmd;
	struct tpm_header *hdr;
	u8 *p, *end;
	u32 nsel, i, n = 0;
	int ret = 0;

	if (b == NULL || algs == NULL || count == NULL)
		return -EINVAL;

	ret = tpm2_alloc_cmd(b, &cmd, TPM_ST_NO_SESSIONS,
			     TPM_CC_GET_CAPABILITY);
	if (ret < 0)
		goto out;

	/* capability, property, propertyCount */
	cmd.params = (u8 *)tpmb_put(b, 3 * sizeof(u32));
	if (cmd.params == NULL) {
		ret = -ENOMEM;
		goto free;
	}

	p = cmd.params;
	*(u32 *)p = cpu_to_be32(TPM_CAP_PCRS);
	p += sizeof(u32);
	*(u32 *)p = cpu_to_be32(0);
	p += sizeof(u32);
	*(u32 *)p = cpu_to_be32(1);

	cmd.header->size = cpu_to_be32(tpmb_size(b));

	if (t->ops.send(b) != tpmb_size(b)) {
		ret = -EAGAIN;
		goto free;
	}

	tpmb_trim(b, tpmb_size(b));
	hdr = (struct tpm_header *)b->head;
	tpmb_put(b, sizeof(struct tpm_header));

	if (t->ops.recv(t->family, b) != tpmb_size(b)) {
		ret = -EAGAIN;
		goto free;
	}

	if (hdr->code != 0) {
		ret = (int)hdr->code;
		goto free;
	}

	/*
	 * Response: moreData, TPMS_CAPABILITY_DATA{capability,
	 * TPML_PCR_SELECTION{count, TPMS_PCR_SELECTION{hash, sizeofSelect,
	 * pcrSelect[]}[]}}. A bank with any PCR selected is allocated.
	 */
	p = b->head + sizeof(struct tpm_header);
	end = b->head + hdr->size;

	if (end - p < 1 + sizeof(u32) + sizeof(u32)) {
		ret = -EIO;
		goto free;
	}
	p += 1;
	if (be32_to_cpu(*(u32 *)p) != TPM_CAP_PCRS) {
		ret = -EIO;
		goto free;
	}
	p += sizeof(u32);
	nsel = be32_to_cpu(*(u32 *)p);
	p += sizeof(u32);

	for (i = 0; i < nsel; i++) {
		u16 alg;
		u8 size, j, selected = 0;

		if (end - p < sizeof(u16) + 1) {
			ret = -EIO;
			goto free;
		}
		alg = be16_to_cpu(*(u16 *)p);
		size = p[sizeof(u16)];
		p += sizeof(u16) + 1;
		if (end - p < size) {
			ret = -EIO;
			goto free;
		}
		for (j = 0; j < size; j++)
			selected |= p[j];
		p += size;

		if (selected) {
			if (n < *count)
				algs[n] = alg;
			n++;
		}
	}

	if (n > *count)
		ret = -ENOSPC;
	else
		*count = n;

free:
	tpmb_free(b);
out:
	return ret;
}

/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __SKINIT_PSP_H__
#define __SKINIT_PSP_H__

#ifdef AMDSL

#include <stdbool.h>

#define	DRTM_MBOX_READY_MASK		0x80000000
#define	DRTM_MBOX_TMR_INDEX_ID_MASK	0x0F000000
#define	DRTM_MBOX_CMD_MASK		0x00FF0000
#define	DRTM_MBOX_STATUS_MASK		0x0000FFFF

#define	DRTM_MBOX_CMD_SHIFT		16

#define	DRTM_NO_ERROR			0x00000000
#define	DRTM_NOT_SUPPORTED		0x00000001
#define	DRTM_LAUNCH_ERROR		0x00000002
#define	DRTM_TMR_SETUP_FAILED_ERROR	0x00000003
#define	DRTM_TMR_DESTROY_FAILED_ERROR	0x00000004
#define	DRTM_GET_TCG_LOGS_FAILED_ERROR	0x00000007
#define	DRTM_OUT_OF_RESOURCES_ERROR	0x00000008
#define	DRTM_GENERIC_ERROR		0x00000009
#define	DRTM_INVALID_SERVICE_ID_ERROR	0x0000000A
#define	DRTM_MEMORY_UNALIGNED_ERROR	0x0000000B
#define	DRTM_MINIMUM_SIZE_ERROR		0x0000000C
#define	DRTM_GET_TMR_DESCRIPTOR_FAILED	0x0000000D
#define	DRTM_EXTEND_OSSL_DIGEST_FAILED	0x0000000E
#define	DRTM_SETUP_NOT_ALLOWED		0x0000000F
#define	DRTM_GET_IVRS_TABLE_FAILED	0x00000010

#define DRTM_CMD_GET_CAPABILITY		0x1
#define	DRTM_CMD_TMR_SETUP		0x2
#define	DRTM_CMD_TMR_RELEASE		0x3
#define	DRTM_CMD_LAUNCH			0x4
#define	DRTM_CMD_GET_TCG_LOGS		0x7
#define	DRTM_CMD_TPM_LOCALITY_ACCESS	0x8
#define	DRTM_CMD_GET_TMR_DESCRIPTORS	0x9
#define	DRTM_CMD_ALLOCATE_SHARED_MEMORY	0xA
#define	DRTM_CMD_EXTEND_OSSL_DIGEST	0xB
#define	DRTM_CMD_GET_IVRS_TABLE_INFO	0xC

#define DRTM_TMR_INDEX_0		0
#define DRTM_TMR_INDEX_1		1
#define DRTM_TMR_INDEX_2		2
#define DRTM_TMR_INDEX_3		3
#define DRTM_TMR_INDEX_4		4
#define DRTM_TMR_INDEX_5		5
#define DRTM_TMR_INDEX_6		6
#define DRTM_TMR_INDEX_7		7

#define	DRTM_CMD_READY			0
#define	DRTM_RESPONSE_READY		1

bool discover_psp(void);

bool drtm_extend_ossl_digest(u64 addr, u64 size);

bool drtm_launch(void);

bool drtm_get_cap(void);

bool drtm_get_tcg_logs(u64 *addr, u64 *size);

/* The first record of the PSP's TCG log: SKINIT's measurement of the SLB */
#define DRTM_TCG_EV_SL_IMAGE		0x8001

#endif /* AMDSL */

#endif /* __SKINIT_PSP_H__ */

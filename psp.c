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

#ifdef AMDSL

#include <defs.h>
#include <boot.h>
#include <types.h>
#include <pci.h>
#include <printk.h>
#include <psp.h>
#include <boot.h>
#include <string.h>

struct psp_drtm_interface {
    volatile u32 *c2pmsg_72;
    volatile u32 *c2pmsg_93;
    volatile u32 *c2pmsg_94;
    volatile u32 *c2pmsg_95;
};

struct psp_drtm_interface g_psp_drtm;

typedef enum {
    PSP_NONE = 0,
    PSP_V1,
    PSP_V2,
    PSP_V3,
    ASP,
} psp_version_t;

struct pci_psp_device {
    u16 vendor_id;
    u16 dev_id;
    psp_version_t version;
};

const struct pci_psp_device psp_devs_list[] = {
    {0x1022, 0x1537, PSP_NONE},
    {0x1022, 0x1456, PSP_V1},
    {0x1022, 0x1468, PSP_NONE},
    {0x1022, 0x1486, PSP_V2},
    {0x1022, 0x15DF, PSP_V3},
    {0x1022, 0x1649, PSP_V2},
    {0x1022, 0x14CA, PSP_V3},
    {0x1022, 0x15C7, PSP_V3},
    {0x1022, 0x156E, ASP},
};

static bool init_drtm_interface(u64 base_addr, psp_version_t psp_version);
static bool drtm_wait_for_psp_ready(u32 *status);

static void drtm_udelay(int us)
{
    while (us--)
        io_delay();
}

static void drtm_print_status(u32 status)
{
    switch(status) {
    case DRTM_NO_ERROR:
        print("DRTM_NO_ERROR");
        break;
    case DRTM_NOT_SUPPORTED:
        print("DRTM_NOT_SUPPORTED");
        break;
    case DRTM_LAUNCH_ERROR:
        print("DRTM_LAUNCH_ERROR");
        break;
    case DRTM_TMR_SETUP_FAILED_ERROR:
        print("DRTM_TMR_SETUP_FAILED_ERROR");
        break;
    case DRTM_TMR_DESTROY_FAILED_ERROR:
        print("DRTM_TMR_DESTROY_FAILED_ERROR");
        break;
    case DRTM_GET_TCG_LOGS_FAILED_ERROR:
        print("DRTM_GET_TCG_LOGS_FAILED_ERROR");
        break;
    case DRTM_OUT_OF_RESOURCES_ERROR:
        print("DRTM_OUT_OF_RESOURCES_ERROR");
        break;
    case DRTM_GENERIC_ERROR:
        print("DRTM_GENERIC_ERROR");
        break;
    case DRTM_INVALID_SERVICE_ID_ERROR:
        print("DRTM_INVALID_SERVICE_ID_ERROR");
        break;
    case DRTM_MEMORY_UNALIGNED_ERROR:
        print("DRTM_MEMORY_UNALIGNED_ERROR");
        break;
    case DRTM_MINIMUM_SIZE_ERROR:
        print("DRTM_MINIMUM_SIZE_ERROR");
        break;
    case DRTM_GET_TMR_DESCRIPTOR_FAILED:
        print("DRTM_GET_TMR_DESCRIPTOR_FAILED");
        break;
    case DRTM_EXTEND_OSSL_DIGEST_FAILED:
        print("DRTM_EXTEND_OSSL_DIGEST_FAILED");
        break;
    case DRTM_SETUP_NOT_ALLOWED:
        print("DRTM_SETUP_NOT_ALLOWED");
        break;
    case DRTM_GET_IVRS_TABLE_FAILED:
        print("DRTM_GET_IVRS_TABLE_FAILED");
        break;
    default:
        print("UNDEFINED");
    }
}

static const struct pci_psp_device *is_drtm_device(u16 vendor_id,
                                                   u16 dev_id)
{
    u32 max_psp_devs = sizeof(psp_devs_list) / sizeof(psp_devs_list[0]);
    const struct pci_psp_device *psp = NULL;
    u32 i;

    for (i = 0; i < max_psp_devs; i++) {
        if ((psp_devs_list[i].vendor_id == vendor_id) &&
            (psp_devs_list[i].dev_id == dev_id)) {
            psp = &psp_devs_list[i];
            break;
        }
    }

    if(psp && psp->version == PSP_NONE) {
        print("DRTM: is_drtm_device: AMD SP device does not have PSP\n");
        psp = NULL;
    }

    return psp;
}

static void smn_register_read (u32 address, u32 *value)
{
  u32 val;

  val = address;
  pci_write(0, 0, 0, 0xB8, 4, val);
  pci_read(0, 0, 0, 0xBC, 4, &val);
  *value = val;
}

#define IOHC0NBCFG_SMNBASE 0x13B00000
#define PSP_BASE_ADDR_LO_SMN_ADDRESS (IOHC0NBCFG_SMNBASE + 0x102E0)
static u64 get_psp_bar_addr (void)
{
  u32 pspbaselo;
  pspbaselo = 0;
  smn_register_read (PSP_BASE_ADDR_LO_SMN_ADDRESS, &pspbaselo);
  //Mask out the lower bits
  pspbaselo &= 0xFFF00000;
  return (u64) pspbaselo;
}

bool discover_psp(void)
{
    u32 bus, slot, func;
    u32 vendor_id, dev_id;
    const struct pci_psp_device *psp = NULL;
    u64 bar2_addr = 0;

    print("DRTM: discover_psp: Entering\n");

    for (bus = 0; bus < PCI_BUSMAX; bus++) {
        for (slot = 0; slot < PCI_SLOTMAX; slot++) {
            for (func = 0; func < PCI_FUNCMAX; func++) {
                if (pci_read(0, bus, PCI_DEVFN(slot, func), 0, 2, &vendor_id))
                    return false;

                if (pci_read(0, bus, PCI_DEVFN(slot, func), 2, 2, &dev_id))
                    return false;

                psp = is_drtm_device((u16)vendor_id, (u16)dev_id);
                if (psp)
                    goto psp_found;
            }
        }
    }

    if (!psp)
        return false;

psp_found:
    print("DRTM: discover_psp: found PSP\n");

    bar2_addr = get_psp_bar_addr();
    if (!bar2_addr)
        return false;

    return init_drtm_interface(bar2_addr, psp->version);
}

static bool init_drtm_interface(u64 base_addr, psp_version_t psp_version)
{
    u64 base = base_addr;

    switch(psp_version) {
    case PSP_V3:
    case PSP_V2:
    case ASP:
        g_psp_drtm.c2pmsg_72 = (volatile u32 *)(u32)(base + 0x10a20);
        g_psp_drtm.c2pmsg_93 = (volatile u32 *)(u32)(base + 0x10a74);
        g_psp_drtm.c2pmsg_94 = (volatile u32 *)(u32)(base + 0x10a78);
        g_psp_drtm.c2pmsg_95 = (volatile u32 *)(u32)(base + 0x10a7c);
        break;
    default:
        print("DRTM: init_drtm_interface: Unrecognized PSP version\n");
        return false;
    }

    print("DRTM: init_drtm_interface: c2pmsg_72 = ");
    print_u64(*g_psp_drtm.c2pmsg_72);
    print("\n");

    return true;
}

/* 100 ms per retry. Hashing a multi-megabyte DLME takes the PSP seconds. */
#define DRTM_WAIT_RETRIES         50
#define DRTM_EXTEND_WAIT_RETRIES  1200

static bool drtm_wait_for_psp_ready_retries(u32 *status, int retry)
{
    u32 reg_val = 0;

    while (--retry) {
        reg_val = *g_psp_drtm.c2pmsg_72;

        if (reg_val & DRTM_MBOX_READY_MASK) {
            break;
        }

        /* TODO: select wait time appropriately */
        drtm_udelay(100000);
    };

    if (!retry) {
        return false;
    }

    if (status) {
        *status = reg_val & 0xffff;
    }

    return true;
}

static bool drtm_wait_for_psp_ready(u32 *status)
{
    return drtm_wait_for_psp_ready_retries(status, DRTM_WAIT_RETRIES);
}

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFF
#endif

#define DRTM_OSSL_RELOC_ADDR 0x08000000

bool drtm_extend_ossl_digest(u64 addr, u64 size)
{
    u32 status = 0;
    u64 reloc_addr = DRTM_OSSL_RELOC_ADDR;

    if (size > UINT32_MAX) {
        print("DRTM: drtm_extend_ossl_digest: OS image too large\n");
        return false;
    }

    print("DRTM: drtm_extend_ossl_digest: addr = ");
    print_u64(addr);
    print(", size = ");
    print_u64(size);
    print("\n");

    if (!(*g_psp_drtm.c2pmsg_72 & DRTM_MBOX_READY_MASK)) {
        print("DRTM: drtm_extend_ossl_digest: PSP not ready\n");
        return false;
    }

    memcpy((void *)(u32)reloc_addr, (void *)(u32)addr, (size_t)size);

    print("DRTM: drtm_extend_ossl_digest: reloc_addr = ");
    print_u64(reloc_addr);
    print(", size = ");
    print_u64(size);
    print("\n");

    *g_psp_drtm.c2pmsg_93 = (u32)size;
    *g_psp_drtm.c2pmsg_94 = (u32)(reloc_addr & 0xFFFFFFFF);
    *g_psp_drtm.c2pmsg_95 = (u32)(reloc_addr >> 32);

    *g_psp_drtm.c2pmsg_72 = (DRTM_CMD_EXTEND_OSSL_DIGEST << DRTM_MBOX_CMD_SHIFT);
    if (!drtm_wait_for_psp_ready_retries(&status, DRTM_EXTEND_WAIT_RETRIES)) {
        print("DRTM: drtm_extend_ossl_digest: command failed to complete\n");
        return false;
    }

    if (status) {
        print("DRTM: drtm_extend_ossl_digest: command failed with status ");
        drtm_print_status(status);
        print("\n");
        return false;
    }

    print("DRTM: drtm_extend_ossl_digest: successfully extended OSSL digest\n");

    return true;
}

bool drtm_launch(void)
{
    u32 status = 0;

    print("DRTM: drtm_launch: Entering\n");

    if (!(*g_psp_drtm.c2pmsg_72 & DRTM_MBOX_READY_MASK)) {
        print("DRTM: drtm_launch: PSP not ready\n");
        return false;
    }

    *g_psp_drtm.c2pmsg_72 = (DRTM_CMD_LAUNCH << DRTM_MBOX_CMD_SHIFT);
    if (!drtm_wait_for_psp_ready(&status)) {
        print("DRTM: drtm_launch: command failed to complete\n");
        return false;
    }

    if (status) {
        print("DRTM: drtm_launch: command failed with status ");
        drtm_print_status(status);
        print("\n");
        return false;
    }

    print("DRTM: drtm_launch: successfully launched\n");

    return true;
}

bool drtm_get_cap(void)
{
    u32 status = 0;

    print("DRTM: drtm_get_cap: Entering\n");

    if (!(*g_psp_drtm.c2pmsg_72 & DRTM_MBOX_READY_MASK)) {
        print("DRTM: drtm_get_cap: PSP not ready\n");
        return false;
    }

    *g_psp_drtm.c2pmsg_72 = (DRTM_CMD_GET_CAPABILITY << DRTM_MBOX_CMD_SHIFT);
    if (!drtm_wait_for_psp_ready(&status)) {
        print("DRTM: drtm_get_cap: command failed to complete\n");
        return false;
    }

    if (status) {
        print("DRTM: drtm_get_cap: command failed with status ");
        drtm_print_status(status);
        print("\n");
        return false;
    }

    print("DRTM: drtm_get_cap: successfully got capability\n");

    print("DRTM: drtm_get_capt: c2pmsg_93 = ");
    print_u64(*g_psp_drtm.c2pmsg_93);
    print(", c2pmsg_94 = ");
    print_u64(*g_psp_drtm.c2pmsg_94);
    print(", c2pmsg_95 = ");
    print_u64(*g_psp_drtm.c2pmsg_95);
    print("\n");

    return true;
}

#endif /* AMDSL */

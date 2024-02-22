/*
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
#include <boot.h>
#include <types.h>
#include <pci.h>
#include <iommu.h>
#include <printk.h>

iommu_dte_t device_table[2 * PAGE_SIZE / sizeof(iommu_dte_t)] __page_data;
iommu_command_t command_buf[2] __aligned(sizeof(iommu_command_t));
char event_log[PAGE_SIZE] __page_data;

u32 iommu_locate(void)
{
    return pci_locate(IOMMU_PCI_BUS,
                      PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION));
}

static void send_command(u64 *mmio_base, iommu_command_t cmd, u32 *idx)
{
    command_buf[(*idx)++] = cmd;
    smp_wmb();
    mmio_base[IOMMU_MMIO_COMMAND_BUF_TAIL] =
        _u(&command_buf[*idx]) - (_u(&command_buf) & ~0xfff);
}

u32 iommu_load_device_table(u32 cap, volatile u64 *completed)
{
    u64 *mmio_base;
    u32 low, hi, i, idx = 0;
    iommu_command_t cmd = {0};

    for (i = 0; i < ARRAY_SIZE(device_table); i++)
        device_table[i].a = IOMMU_DTE_Q0_V + IOMMU_DTE_Q0_TV;

    pci_read(0, IOMMU_PCI_BUS,
             PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
             IOMMU_CAP_BA_LOW(cap),
             4, &low);

    /* IOMMU must be enabled by AGESA */
    if ( (low & IOMMU_CAP_BA_LOW_ENABLE) == 0 )
        return 1;

    pci_read(0, IOMMU_PCI_BUS,
             PCI_DEVFN(IOMMU_PCI_DEVICE, IOMMU_PCI_FUNCTION),
             IOMMU_CAP_BA_HIGH(cap),
             4, &hi);

    mmio_base = _p((u64)hi << 32 | (low & 0xffffc000));

    print_u64((u64)_u(mmio_base));
    print("IOMMU MMIO Base Address\n");

    print_u64(mmio_base[IOMMU_MMIO_STATUS_REGISTER]);
    print("IOMMU_MMIO_STATUS_REGISTER\n");

    /* Disable IOMMU and all its features */
    mmio_base[IOMMU_MMIO_CONTROL_REGISTER] &= ~IOMMU_CR_ENABLE_ALL_MASK;
    smp_wmb();

    /* Address and size of Device Table (bits 8:0 = 0 -> 4KB; 1 -> 8KB ...) */
    mmio_base[IOMMU_MMIO_DEVICE_TABLE_BA] = (u64)_u(device_table) | 1;

    print_u64(mmio_base[IOMMU_MMIO_DEVICE_TABLE_BA]);
    print("IOMMU_MMIO_DEVICE_TABLE_BA\n");

    /*
     * !!! WARNING - HERE BE DRAGONS !!!
     *
     * Address and size of Command Buffer, reset head and tail registers.
     *
     * The IOMMU command buffer is required to be an aligned power of two,
     * with a minimum size of 4k.  We only need to send a handful of
     * commands, and really don't have 4k worth of space to spare.
     * Furthermore, the buffer is only ever read by the IOMMU.
     *
     * Therefore, we have a small array of command buffer entries, aligned
     * on the size of one entry.  We program the IOMMU to say that the
     * command buffer is 8k long (to cover the case that the array crosses
     * a page boundary), and move both the head and tail pointers forwards
     * to the start of the buffer.
     *
     * This will malfunction if more commands are sent than fit in
     * command_buf[] to begin with, but we do save almost 4k of space,
     * 1/16th of that available to us.
     */
    mmio_base[IOMMU_MMIO_COMMAND_BUF_BA] = (u64)(_u(command_buf) & ~0xfff)| (0x9ULL << 56);
    mmio_base[IOMMU_MMIO_COMMAND_BUF_HEAD] =
        mmio_base[IOMMU_MMIO_COMMAND_BUF_TAIL] = _u(command_buf) & 0xff0;

    print_u64(_u(command_buf));
    print("Command Buffer Base\n");

    print_u64(mmio_base[IOMMU_MMIO_COMMAND_BUF_BA]);
    print("IOMMU_MMIO_COMMAND_BUF_BA\n");

    print_u64(mmio_base[IOMMU_MMIO_COMMAND_BUF_HEAD]);
    print("IOMMU_MMIO_COMMAND_BUF_HEAD\n");

    /* Address and size of Event Log, reset head and tail registers */
    mmio_base[IOMMU_MMIO_EVENT_LOG_BA] = (u64)_u(event_log) | (0x8ULL << 56);
    mmio_base[IOMMU_MMIO_EVENT_LOG_HEAD] = 0;
    mmio_base[IOMMU_MMIO_EVENT_LOG_TAIL] = 0;

    print_u64(mmio_base[IOMMU_MMIO_EVENT_LOG_BA]);
    print("IOMMU_MMIO_EVENT_LOG_BA\n");

    /* Clear EventLogInt set by IOMMU not being able to read command buffer */
    mmio_base[IOMMU_MMIO_STATUS_REGISTER] &= ~2;
    smp_wmb();
    mmio_base[IOMMU_MMIO_CONTROL_REGISTER] |= IOMMU_CR_CmdBufEn | IOMMU_CR_EventLogEn;
    smp_wmb();

    mmio_base[IOMMU_MMIO_CONTROL_REGISTER] |= IOMMU_CR_IommuEn;

    print_u64(mmio_base[IOMMU_MMIO_STATUS_REGISTER]);
    print("IOMMU_MMIO_STATUS_REGISTER\n");

    if ( mmio_base[IOMMU_MMIO_EXTENDED_FEATURE] & IOMMU_EF_IASup )
    {
        print("INVALIDATE_IOMMU_ALL\n");
        cmd.opcode = INVALIDATE_IOMMU_ALL;
        send_command(mmio_base, cmd, &idx);
    } /* TODO: else? */

    print_u64(mmio_base[IOMMU_MMIO_EXTENDED_FEATURE]);
    print("IOMMU_MMIO_EXTENDED_FEATURE\n");
    print_u64(mmio_base[IOMMU_MMIO_STATUS_REGISTER]);
    print("IOMMU_MMIO_STATUS_REGISTER\n");

    /* Write to a variable inside SLB (does not work in the first call) */
    cmd.u0 = _u(completed) | 1;
    /* This should be '_u(completed)>>32', but SLB can't be above 4GB anyway */
    cmd.u1 = 0;

    cmd.opcode = COMPLETION_WAIT;
    cmd.u2 = 0x656e6f64;    /* "done" */
    send_command(mmio_base, cmd, &idx);

    print_u64(mmio_base[IOMMU_MMIO_STATUS_REGISTER]);
    print("IOMMU_MMIO_STATUS_REGISTER\n");

    return 0;
}

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
#include <dev.h>

/*
 * There are only 5 bits (0x00..0x1f) for PCI slot number (see definition of
 * PCI_DEVFN) and we start at 0x18 (DEV_PCI_DEVICE), so there is hard upper
 * limit on how many nodes can exist.
 */
#define MAX_CPU_NODES 8

u32 dev_locate(u8 cpu_node)
{
       return pci_locate(DEV_PCI_BUS,
                         PCI_DEVFN(DEV_PCI_DEVICE + cpu_node, DEV_PCI_FUNCTION));
}

u32 dev_read(u8 cpu_node, u32 dev_cap, u32 function, u32 index)
{
    u32 value;

    pci_write(0, DEV_PCI_BUS,
              PCI_DEVFN(DEV_PCI_DEVICE + cpu_node, DEV_PCI_FUNCTION),
              dev_cap + DEV_OP_OFFSET,
              4,
              (u32)(((function & 0xff) << 8) + (index & 0xff)));

    pci_read(0, DEV_PCI_BUS,
             PCI_DEVFN(DEV_PCI_DEVICE + cpu_node, DEV_PCI_FUNCTION),
             dev_cap + DEV_DATA_OFFSET,
             4, &value);

       return value;
}

void dev_write(u8 cpu_node, u32 dev, u32 function, u32 index, u32 value)
{
    pci_write(0, DEV_PCI_BUS,
        PCI_DEVFN(DEV_PCI_DEVICE + cpu_node, DEV_PCI_FUNCTION),
        dev + DEV_OP_OFFSET,
        4,
        (u32)(((function & 0xff) << 8) + (index & 0xff)) );

    pci_write(0, DEV_PCI_BUS,
        PCI_DEVFN(DEV_PCI_DEVICE + cpu_node, DEV_PCI_FUNCTION),
        dev + DEV_DATA_OFFSET,
        4, value);
}

void dev_disable_sl(u8 cpu_node, u32 dev)
{
    u32 dev_cr = dev_read(cpu_node, dev, DEV_CR, 0);
    dev_write(cpu_node, dev, DEV_CR, 0, dev_cr & ~(DEV_CR_SL_DEV_EN_MASK));
}

void disable_memory_protection(void)
{
    u32 dev_cap, sldev, vid_did;
    u8 cpu_node = 0;

    dev_cap = dev_locate(cpu_node);
    if (dev_cap) {
        /* Older families with remains of DEV */
        do {
            dev_disable_sl(cpu_node, dev_cap);

            cpu_node++;
            if (cpu_node == MAX_CPU_NODES)
                break;

            dev_cap = dev_locate(cpu_node);
        } while (dev_cap);
        return;
    }

    /* Fam 17h uses different DMA protection control register */
    while (cpu_node < MAX_CPU_NODES &&
           pci_read(0, MCH_PCI_BUS,
                    PCI_DEVFN(MCH_PCI_DEVICE + cpu_node, MCH_PCI_FUNCTION),
                    VIDDID, 4, &vid_did) == 0 &&
           vid_did != 0xffffffffU) {
        u8 devfn = PCI_DEVFN(MCH_PCI_DEVICE + cpu_node, MCH_PCI_FUNCTION);

        pci_read(0, MCH_PCI_BUS, devfn, MEMPROT_CR, 4, &sldev);
        pci_write(0, MCH_PCI_BUS, devfn, MEMPROT_CR, 4, sldev & ~(MEMPROT_EN));

        cpu_node++;
    }
}


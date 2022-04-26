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

u32 dev_locate(void)
{
       return pci_locate(DEV_PCI_BUS,
                         PCI_DEVFN(DEV_PCI_DEVICE, DEV_PCI_FUNCTION));
}

u32 dev_read(u32 dev_cap, u32 function, u32 index)
{
        u32 value;

        pci_write(0, DEV_PCI_BUS,
                       PCI_DEVFN(DEV_PCI_DEVICE,DEV_PCI_FUNCTION),
                       dev_cap + DEV_OP_OFFSET,
                       4,
                       (u32)(((function & 0xff) << 8) + (index & 0xff)) );

        pci_read(0, DEV_PCI_BUS,
                       PCI_DEVFN(DEV_PCI_DEVICE,DEV_PCI_FUNCTION),
                       dev_cap + DEV_DATA_OFFSET,
                       4, &value);

       return value;
}

void dev_write(u32 dev, u32 function, u32 index, u32 value)
{
        pci_write(0, DEV_PCI_BUS,
			PCI_DEVFN(DEV_PCI_DEVICE,DEV_PCI_FUNCTION),
			dev + DEV_OP_OFFSET,
			4,
			(u32)(((function & 0xff) << 8) + (index & 0xff)) );

        pci_write(0, DEV_PCI_BUS,
			PCI_DEVFN(DEV_PCI_DEVICE,DEV_PCI_FUNCTION),
			dev + DEV_DATA_OFFSET,
			4, value);
}

void dev_disable_sl(u32 dev)
{
	u32 dev_cr = dev_read(dev, DEV_CR, 0);
	dev_write(dev, DEV_CR, 0, dev_cr & ~(DEV_CR_SL_DEV_EN_MASK));
}

void disable_memory_protection(void)
{
       u32 dev_cap, sldev;

       dev_cap = dev_locate();
       if (dev_cap) {
               /* Older families with remains of DEV */
               dev_disable_sl(dev_cap);
               return;
       }

       /* Fam 17h uses different DMA protection control register */
       pci_read(0, MCH_PCI_BUS,
                PCI_DEVFN(MCH_PCI_DEVICE, MCH_PCI_FUNCTION),
                MEMPROT_CR, 4, &sldev);
       pci_write(0, MCH_PCI_BUS,
                 PCI_DEVFN(MCH_PCI_DEVICE, MCH_PCI_FUNCTION),
                 MEMPROT_CR, 4, sldev & ~(MEMPROT_EN));
}


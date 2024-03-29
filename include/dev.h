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

#ifndef __DEV_H__
#define __DEV_H__

#include <types.h>

/* Following are used to disable initial SLB protection only */

#define DEV_PCI_BUS		0x0
#define DEV_PCI_DEVICE		0x18
#define DEV_PCI_FUNCTION	0x3

#define DEV_OP_OFFSET		4
#define DEV_DATA_OFFSET		8

#define DEV_CR			4

#define DEV_CR_SL_DEV_EN_MASK	1<<5

/* Family 17h and newer */
#define MCH_PCI_BUS		0x0
#define MCH_PCI_DEVICE		0x18
#define MCH_PCI_FUNCTION	0x0

#define VIDDID			0
#define MEMPROT_CR		0x384

#define MEMPROT_EN		(1<<0)

u32 dev_locate(u8 cpu_node);
void dev_disable_sl(u8 cpu_node, u32 dev);
void disable_memory_protection(void);

#endif /* __DEV_H__ */

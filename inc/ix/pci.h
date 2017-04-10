/*
 * Copyright (c) 2015-2017, Stanford University
 *  
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * pci.h - PCI bus support
 */

#pragma once

#include <ix/types.h>

struct pci_bar {
	uint64_t start;	/* the start address, or zero if no resource */
	uint64_t len;	/* the length of the resource */
	uint64_t flags; /* Linux resource flags */
};

/* NOTE: these are the same as the Linux PCI sysfs resource flags */
#define PCI_BAR_IO		0x00000100
#define PCI_BAR_MEM		0x00000200
#define PCI_BAR_PREFETCH	0x00002000 /* typically WC memory */
#define PCI_BAR_READONLY	0x00004000 /* typically option ROMs */

#define SYSFS_PCI_PATH          "/sys/bus/pci/devices"

#define PCI_MAX_BARS 7

struct pci_addr {
	uint16_t domain;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
};

extern int pci_str_to_addr(const char *str, struct pci_addr *addr);

struct pci_dev {
	struct pci_addr addr;

	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;

	struct pci_bar bars[PCI_MAX_BARS];
	int numa_node;
	int max_vfs;
};

extern struct pci_dev *pci_alloc_dev(const struct pci_addr *addr);
extern struct pci_bar *pci_find_mem_bar(struct pci_dev *dev, int count);
extern void *pci_map_mem_bar(struct pci_dev *dev, struct pci_bar *bar, bool wc);
extern void pci_unmap_mem_bar(struct pci_bar *bar, void *vaddr);
extern int pci_enable_device(struct pci_dev *dev);
extern int pci_set_master(struct pci_dev *dev);
extern int ix_pci_device_cfg_read_u32(struct pci_dev* handle, uint32_t* var, uint32_t offset);
extern int ix_pci_device_cfg_write_u32(struct pci_dev* handle, uint32_t var, uint32_t offset);

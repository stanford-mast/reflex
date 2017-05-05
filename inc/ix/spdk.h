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

/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __NVME_IMPL_H__
#define __NVME_IMPL_H__

#include <assert.h>
#include <time.h>
#include <unistd.h>

#include <ix/mempool.h>
#include <ix/page.h>
#include <ix/lock_recursive.h>
#include <ix/lock.h>
#include <ix/log.h>
#include <ix/pci.h>

#include <pciaccess.h>
//XXXremove below
#include "rte_pci.h"

#include "spdk/pci.h"
#include "spdk/pci_ids.h"

//FIXME: Adapt to IX
#define NVME_VTOPHYS_ERROR       (0xFFFFFFFFFFFFFFFFULL)

/**
 * Get a monotonic timestamp counter (used for measuring timeouts during initialization).
 */
#define nvme_get_tsc()			rdtsc()

/**
 * Get the tick rate of nvme_get_tsc() per second.
 */
#define nvme_get_tsc_hz()		2300 * 1000 * 1000

//the main nvme dev
extern struct pci_dev *g_nvme_dev;

/**
 * \file
 *
 * This file describes the callback functions required to integrate
 * the userspace NVMe driver for a specific implementation.  This
 * implementation is specific for DPDK for Storage.  Users would
 * revise it as necessary for their own particular environment if not
 * using it within the DPDK for Storage framework.
 */

/**
 * \page nvme_driver_integration NVMe Driver Integration
 *
 * Users can integrate the userspace NVMe driver into their environment
 * by implementing the callbacks in nvme_impl.h.  These callbacks
 * enable users to specify how to allocate pinned and physically
 * contiguous memory, performance virtual to physical address
 * translations, log messages, PCI configuration and register mapping,
 * and a number of other facilities that may differ depending on the
 * environment.
 */

/**
 * Allocate a pinned, physically contiguous memory buffer with the
 *   given size and alignment.
 * Note: these calls are only made during driver initialization.  Per
 *   I/O allocations during driver operation use the nvme_alloc_request
 *   callback.
 */
static inline void *
nvme_malloc(const char *tag, size_t size, unsigned align, uint64_t *phys_addr)
{
	void* buf;
	static long offset = PGSIZE_2MB;
	static void* cur_addr;
	static uint64_t phys_base;
	
	if(size>PGSIZE_2MB) {
		log_info("Allocate memory failed size: %lx\n", size);
		assert(0);
	}

	/*
	 * TODO: Add proper alloctor IX that allows to allocate memory
	 * smaller than 2MB. Right now we allign each allocation here
	 * to 4KB, which is required for Samsung devices. I am not sure
	 * whether this is the nvme spec and what other requirements regarding
	 * alignment there exist for other future devs, but for now this works
	 */
	
	if(size & (PGSIZE_4KB - 1)) {
		size &= ~(PGSIZE_4KB - 1);
		size += PGSIZE_4KB;
	}

	//IX only support 2MB pages so we implement a trivial allocator:
        //fill up pages until they overflow then get the next page
	if((offset+size)>PGSIZE_2MB) {
		buf = page_alloc_contig(1);
		memset(buf, 0, PGSIZE_2MB);
		cur_addr = buf;
	    	offset = size;
		phys_base = (uint64_t)page_get(buf); //pins and gets MA
		*phys_addr = phys_base;
	}
	else {
		buf = cur_addr + offset;
		*phys_addr = phys_base + offset;
		offset += size;
	}
	return buf;
}

static inline void
addr_free(void* addr){
	page_free(addr);
}

/**
 * Free a memory buffer previously allocated with nvme_malloc.
 */
#define nvme_free(buf)			addr_free(buf)

/**
 * Log or print a message from the NVMe driver.
 */
#define nvme_printf(ctrlr, fmt, args...) log_debug(fmt, ##args)

/**
 * Assert a condition and panic/abort as desired.  Failures of these
 *  assertions indicate catastrophic failures within the driver.
 */
#define nvme_assert(check, str) assert(check)

/**
 * Return the physical address for the specified virtual address.
 */
#define nvme_vtophys(buf)		page_machaddr(buf)

DECLARE_PERCPU(struct mempool, request_mempool);

extern struct nvme_request * alloc_local_nvme_request(struct nvme_request **);
extern void free_local_nvme_request(struct nvme_request *req);


/**
 * Return a buffer for an nvme_request object.  These objects are allocated
 *  for each I/O.  They do not need to be pinned nor physically contiguous.
 */
#define nvme_alloc_request(bufp) alloc_local_nvme_request(bufp)

/**
 * Free a buffer previously allocated with nvme_alloc_request().
 */
#define nvme_dealloc_request(buf) free_local_nvme_request(buf)

/**
 *
 */
#define nvme_pcicfg_read32(handle, var, offset)  ix_pci_device_cfg_read_u32((struct pci_dev *)handle, var, offset)
#define nvme_pcicfg_write32(handle, var, offset) ix_pci_device_cfg_write_u32((struct pci_dev *)handle, var, offset)

struct nvme_pci_enum_ctx {
	int (*user_enum_cb)(void *enum_ctx, struct spdk_pci_device *pci_dev);
	void *user_enum_ctx;
};

static inline int
nvme_pcicfg_map_bar(void *devhandle, uint32_t bar, uint32_t read_only, void **mapped_addr)
{
	struct pci_dev *dev = devhandle;
	struct pci_bar* pbar =  &dev->bars[bar];
	
	//	assert(dev->vendor_id==0x8086); //Only Intel devs for now
	
	bool wc = false;
	*mapped_addr = pci_map_mem_bar(dev, pbar, wc);
	log_info("maped bar: %p dev %p pbar %i %i\n", *mapped_addr, dev, pbar->start, pbar->len);
	if(mapped_addr)
		return 0;
	else
		return -1;
}

static inline int
nvme_pcicfg_map_bar_write_combine(void *devhandle, uint32_t bar, void **mapped_addr)
{
	struct pci_dev *dev = devhandle;
	struct pci_bar* pbar =  &dev->bars[bar];
	
	//	assert(dev->vendor_id==0x8086); //Only Intel devs for now
	log_info("nvme pci dev map write combine, not sure if this works leave WC disabled\n");
	bool wc = false;
	*mapped_addr = pci_map_mem_bar(dev, pbar, wc);

	if(mapped_addr)
		return 0;
	else
		return -1;

}


static inline int
nvme_pcicfg_unmap_bar(void *devhandle, uint32_t bar, void *addr)
{
	struct pci_dev *dev = devhandle;
	struct pci_bar* pbar =  &dev->bars[bar];
	
	pci_unmap_mem_bar(pbar, addr);
	return 0;
}

static inline void
nvme_pcicfg_get_bar_addr_len(void *devhandle, uint32_t bar, uint64_t *addr, uint64_t *size)
{
	struct pci_dev *dev = devhandle;
	struct pci_bar* pbar =  &dev->bars[bar];
	
	*addr = pbar->start;
	*size = pbar->len;
}

/*
 * TODO: once DPDK supports matching class code instead of device ID, switch to SPDK_PCI_CLASS_NVME
 */
/*
static struct rte_pci_id nvme_pci_driver_id[] = {
	{RTE_PCI_DEVICE(0x8086, 0x0953)},
	//{RTE_PCI_DEVICE(0x144d, 0xa821)},
	{ .vendor_id = 0,  },
};
*/
/*
 * TODO: eliminate this global if possible (does rte_pci_driver have a context field for this?)
 *
 * This should be protected by the NVMe driver lock, since nvme_probe() holds the lock
 *  while calling nvme_pci_enumerate(), but we shouldn't have to depend on that.
 */
//static struct nvme_pci_enum_ctx g_nvme_pci_enum_ctx;
/*
static int
nvme_driver_init(struct rte_pci_driver *dr, struct rte_pci_device *rte_dev)
{
	struct spdk_pci_device *pci_dev = (struct spdk_pci_device *)rte_dev;
	log_info("nvme driver init\n");
	usleep(500 * 1000);

	return g_nvme_pci_enum_ctx.user_enum_cb(g_nvme_pci_enum_ctx.user_enum_ctx, pci_dev);
}*/
/*
static struct rte_pci_driver nvme_rte_driver = {
	.name = "nvme_driver",
	.devinit = nvme_driver_init,
	.id_table = nvme_pci_driver_id,
	.drv_flags = RTE_PCI_DRV_NEED_MAPPING,
	};*/

/*
static inline int
nvme_pci_enumerate(int (*enum_cb)(void *enum_ctx, struct spdk_pci_device *pci_dev), void *enum_ctx)
{
	int rc;

	g_nvme_pci_enum_ctx.user_enum_cb = enum_cb;
	g_nvme_pci_enum_ctx.user_enum_ctx = enum_ctx;
	log_info("register nvme driver\n");
	rte_eal_pci_register(&nvme_rte_driver);
	rc = rte_eal_pci_probe();
//	rte_eal_pci_unregister(&nvme_rte_driver);

	return rc;
}
*/
static int
nvme_pci_enum_cb(void *enum_ctx, struct spdk_pci_device *pci_dev)
{
	struct nvme_pci_enum_ctx *ctx = enum_ctx;

	//usleep(500 * 1000);
	log_info("user %p res %lx\n", ctx->user_enum_ctx, ctx->user_enum_cb(ctx->user_enum_ctx, pci_dev));

	if (spdk_pci_device_get_class(pci_dev) != SPDK_PCI_CLASS_NVME) {
		return 0;
	}

	return ctx->user_enum_cb(ctx->user_enum_ctx, pci_dev);
}

static inline int
nvme_pci_enumerate(int (*enum_cb)(void *enum_ctx, struct spdk_pci_device *pci_dev), void *enum_ctx)
{
	
	struct nvme_pci_enum_ctx nvme_enum_ctx;

	nvme_enum_ctx.user_enum_cb = enum_cb;
	nvme_enum_ctx.user_enum_ctx = enum_ctx;
	
	log_info("pci enumerate dev %lx\n", g_nvme_dev);
	nvme_pci_enum_cb((void *)&nvme_enum_ctx, (struct spdk_pci_device *)g_nvme_dev); 
//	int ret = spdk_pci_enumerate(nvme_pci_enum_cb, &nvme_enum_ctx);
	
//		what is the g_nvme_driver?
	return 0;
}


typedef spinlock_rec_t nvme_mutex_t;

#define nvme_mutex_init(x) spin_lock_recursive_init(x) 
#define nvme_mutex_destroy(x) 
#define nvme_mutex_lock spin_lock_recursive
#define nvme_mutex_unlock spin_unlock_recursive
#define NVME_MUTEX_INITIALIZER SPINLOCK_INITIALIZER_RECURSIVE

static inline int
nvme_mutex_init_recursive(nvme_mutex_t *mtx)
{
	spin_set_recursive(mtx);
	return 0;
/*	pthread_mutexattr_t attr;
	int rc = 0;

	if (pthread_mutexattr_init(&attr)) {
		return -1;
	}
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) ||
	    pthread_mutex_init(mtx, &attr)) {
		rc = -1;
	}
	pthread_mutexattr_destroy(&attr);
	return rc;*/
}

/**
 * Copy a struct nvme_command from one memory location to another.
 */
//#define nvme_copy_command(dst, src)	memcpy((dst), (src), sizeof(struct spdk_nvme_cmd))

#endif /* __NVME_IMPL_H__ */

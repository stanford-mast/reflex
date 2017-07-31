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
 * mbuf.h - buffer management for network packets
 */

#pragma once
#include <sys/socket.h>
#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_per_lcore.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include <ix/stddef.h>
//#include <ix/mem.h>
#include <ix/mempool.h>
#include <ix/cpu.h>
//#include <ix/page.h>
#include <ix/syscall.h>

// Taken from mem.h
#ifndef MAP_FAILED
#define MAP_FAILED      ((void *) -1)
#endif

typedef unsigned long machaddr_t; /* host physical addresses */
enum {
	PGSHIFT_4KB = 12,
	PGSHIFT_2MB = 21,
	PGSHIFT_1GB = 30,
};

enum {
	PGSIZE_4KB = (1 << PGSHIFT_4KB), /* 4096 bytes */
	PGSIZE_2MB = (1 << PGSHIFT_2MB), /* 2097152 bytes */
	PGSIZE_1GB = (1 << PGSHIFT_1GB), /* 1073741824 bytes */
};


struct mbuf_iov {
	void *base;
	machaddr_t maddr;
	size_t len;
};

/**
 * mbuf_iov_create - creates an mbuf IOV and references the IOV memory
 * @iov: the IOV to create
 * @ent: the reference sg entry
 *
 * Returns the length of the mbuf IOV (could be less than the sg entry).
 */

static inline size_t
mbuf_iov_create(struct mbuf_iov *iov, struct sg_entry *ent)
{
	
	size_t len = min(ent->len, PGSIZE_2MB - PGOFF_2MB(ent->base));

	log_info("mbuf_iov_create().....\n");
	iov->base = ent->base;
	// TODO: Not sure if okay
	//iov->maddr = page_get(ent->base);
	iov->maddr = ent->base;
	iov->len = len;	

	return len;
}

/**
 * mbuf_iov_free - unreferences the IOV memory
 * @iov: the IOV
 */

static inline void mbuf_iov_free(struct mbuf_iov *iov)
{
	// TODO: Maybe something else should be done here
	//page_put(iov->base);
}


#define MBUF_INVALID_FG_ID 0xFFFF

// NOTE: dpdk_mbuf pointer points to a rte_mbuf
struct mbuf {
	//size_t len;		// the length of the mbuf data
	struct mbuf *next;	// the next buffer of the packet
				// (can happen with recieve-side coalescing)
	struct mbuf_iov *iovs;	// transmit scatter-gather array
	unsigned int nr_iov;	// the number of scatter-gather vectors

	uint16_t fg_id;		// the flow group identifier
	uint16_t ol_flags;	// which offloads to enable?

	void (*done)(struct mbuf *m);  // called on free
	unsigned long done_data; // extra data to pass to done()
	unsigned long timestamp; // receive timestamp (in CPU clock ticks)

	struct rte_mbuf *dpdk_mbuf;   // rte_mbuf form dpdk
};


#define MBUF_HEADER_LEN		64	/* one cache line */
#define MBUF_DATA_LEN		9000 /* Originally 2048	(2 KB) but increase for jumbo frame support */
#define MBUF_LEN		(MBUF_HEADER_LEN + MBUF_DATA_LEN)

// Alternative MBUF size for rte_mbufs
#define DPDK_MBUF_SIZE       (MBUF_DATA_LEN + RTE_PKTMBUF_HEADROOM)


/**
 * mbuf_mtod_off - cast a pointer to the data with an offset
 * @m: the mbuf
 * @type: the type to cast
 * @off: the offset
 */
#define mbuf_mtod_off(m, type, off) \
	((type) ((uintptr_t) (m) + MBUF_HEADER_LEN + (off)))

/**
 * mbuf_mtod - cast a pointer to the beginning of the data
 * @mbuf: the mbuf
 * @type: the type to cast
 */
// Replaced with rte_mbuf function
/*
#define mbuf_mtod(mbuf, type) \
	((type) ((uintptr_t) (mbuf) + MBUF_HEADER_LEN))
*/
#define mbuf_mtod(m, type) \
	((type)((char *)(m)->buf_addr + (m)->data_off + (0)))

/**
 * mbuf_nextd_off - advance a data pointer by an offset
 * @ptr: the starting data pointer
 * @type: the new type to cast
 * @off: the offset
 */
#define mbuf_nextd_off(ptr, type, off) \
	((type) ((uintptr_t) (ptr) + (off)))

/**
 * mbuf_nextd - advance a data pointer to the end of the current type
 * @ptr: the starting data pointer
 * @type: the new type to cast
 *
 * Automatically infers the size of the starting data structure.
 */
#define mbuf_nextd(ptr, type) \
	mbuf_nextd_off(ptr, type, sizeof(typeof(*ptr)))

/**
 * mbuf_enough_space - determines if the buffer is large enough
 * @mbuf: the mbuf
 * @pos: a pointer to the current position in the mbuf data
 * @sz: the length to go past the current position
 *
 * Returns true if there is room, otherwise false.
 */
#define mbuf_enough_space(mbuf, pos, sz) \
	((uintptr_t) (pos) - (mbuf_mtod(mbuf, uintptr_t)) + (sz) <= \
	 rte_pktmbuf_data_len(mbuf))
	 
//rte_pktmbuf_data_len(mbuf->dpdk_mbuf))

extern void mbuf_default_done(struct mbuf *m);

RTE_DECLARE_PER_LCORE(struct mempool, mbuf_mempool);

/**
 * mbuf_alloc - allocate an mbuf from a memory pool
 * @pool: the memory pool
 *
 * Returns an mbuf, or NULL if failure.
 */
static inline struct rte_mbuf *mbuf_alloc(struct mempool *pool, int socket)
{
	struct rte_mbuf *dpdk_mbuf = rte_pktmbuf_alloc(pool->datastore->pool);
	if (unlikely(!dpdk_mbuf))
		return NULL;

	dpdk_mbuf->data_len = 0; 
	dpdk_mbuf->pkt_len = 0;
	dpdk_mbuf->next = NULL;
	
	return dpdk_mbuf;
}

/**
 * mbuf_free - frees an mbuf
 * @m: the mbuf
 */
static inline void mbuf_free(struct rte_mbuf *m)
{
	rte_pktmbuf_free(m);
}

/**
 * mbuf_get_data_machaddr - get the machine address of the mbuf data
 * @m: the mbuf
 *
 * Returns a machine address.
 */
static inline struct rte_mbuf* mbuf_get_data_machaddr(struct rte_mbuf *m)
{
	return mbuf_mtod(m, void*);
}


/**
 * mbuf_xmit_done - called when a TX queue completes an mbuf
 * @m: the mbuf
 */
static inline void mbuf_xmit_done(struct mbuf *m)
{
	m->done(m);
}

/**
 * mbuf_alloc_local - allocate an mbuf from the core-local mempool
 *
 * Returns an mbuf, or NULL if out of memory.
 */
static inline struct rte_mbuf *mbuf_alloc_local(void)
{
	int socket = rte_socket_id();
	return mbuf_alloc(&percpu_get(mbuf_mempool), socket);
}

extern int mbuf_init(void);
extern int mbuf_init_cpu(void);
extern void mbuf_exit_cpu(void);

/*
 * direct dispatches into network stack
 * FIXME: add a function for each entry point (e.g. udp and tcp)
 */
struct eth_rx_queue;

extern void eth_input(struct eth_rx_queue *rx_queue, struct mbuf *pkt);

extern void eth_input_process(struct rte_mbuf *pkt, int nb_pkts);


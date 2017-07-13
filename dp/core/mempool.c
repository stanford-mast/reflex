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
 * Mempool.c - A fast per-CPU memory pool implementation
 *
 * A mempool is a heap that only supports allocations of a
 * single fixed size. Generally, mempools are created up front
 * with non-trivial setup cost and then used frequently with
 * very low marginal allocation cost.
 *
 * Mempools are not thread-safe. This allows them to be really
 * efficient but you must provide your own locking if you intend
 * to use a memory pool with more than one core.
 *
 * Mempools rely on a data store -- which is where the allocation
 * happens.  The backing store is thread-safe, meaning that you
 * typically have datastore per NUMA node and per data type.
 *
 * Mempool datastores also
 * have NUMA affinity to the core they were created for, so
 * using them from the wrong core frequently could result in
 * poor performance.
 *
 * The basic implementation strategy is to partition a region of
 * memory into several fixed-sized slots and to maintain a
 * singly-linked free list of available slots. Objects are
 * allocated in LIFO order so that preference is given to the
 * free slots that are most cache hot.
 */

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_errno.h>

#include <ix/stddef.h>
#include <ix/errno.h>
#include <ix/mempool.h>
#include <stdio.h>
#include <ix/log.h>
#include <ix/timer.h>

static struct mempool_datastore *mempool_all_datastores;
#ifdef ENABLE_KSTATS
static struct timer mempool_timer;
#endif

/**
 * mempool_create_datastore - initializes a memory pool datastore
 * @nr_elems: the minimum number of elements in the total pool
 * @elem_len: the length of each element
 *
 * NOTE: mempool_createdatastore() will create a pool with at least @nr_elems,
 * but possibily more depending on page alignment.
 *
 * There should be one datastore per C data type (in general).
 * Each core, flow-group or unit of concurrency will create a distinct mempool leveraging the datastore
 *
 * Returns 0 if successful, otherwise fail.
 */

int mempool_create_datastore(struct mempool_datastore *mds, int nr_elems, size_t elem_len, const char *name)
{
	//int nr_pages;

	assert(mds->magic == 0);


	if (!elem_len || !nr_elems)
		return -EINVAL;

	mds->magic = MEMPOOL_MAGIC;
	mds->prettyname = name;
	elem_len = align_up(elem_len, sizeof(long)) + MEMPOOL_INITIAL_OFFSET;

	// Arguments for mempool constructor in case they need to be changed later
	unsigned cache_size = RTE_MEMPOOL_CACHE_MAX_SIZE;
	unsigned private_data_size = 0;
	rte_mempool_ctor_t* mp_init = NULL;
	void* mp_init_arg = NULL;
	rte_mempool_obj_ctor_t* obj_init = NULL;
	void* obj_init_arg = NULL;
	int socket_id = rte_socket_id();
	unsigned flags = MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET; //0;
	mds->pool = rte_mempool_create(name, nr_elems, elem_len, cache_size, private_data_size, mp_init, mp_init_arg, obj_init, obj_init_arg, socket_id, flags);
	//mds->pool = rte_pktmbuf_pool_create(name, nr_elems, cache_size, 0, elem_len, rte_socket_id());

	
	mds->nr_elems = nr_elems;
	mds->elem_len = elem_len;
	
	if (mds->pool == NULL) {
		log_err("mempool alloc failed\n");
		printf("Unable to create mempool datastore %s | nr_elems: %d | elem_len: %lu | Error: %s\n", name, nr_elems, elem_len, rte_strerror(rte_errno));


		panic("unable to create mempool datastore for %s\n", name);
		return -ENOMEM;
	}

	mds->next_ds = mempool_all_datastores;
	mempool_all_datastores = mds;

	printf("mempool_datastore: %-15s elem_len:%lu nr_elems:%d\n",
	       name,
	       mds->elem_len,
	       nr_elems);

	return 0;
}

// TODO: This is a copy of the mempool_create_datastore function with rte_mbuf constructors.
// The original mempool_create_datastore function should just take arguments to support this function.
/**
 * mempool_create_datastore - initializes a memory pool datastore
 * @nr_elems: the minimum number of elements in the total pool
 * @elem_len: the length of each element
 *
 * NOTE: mempool_createdatastore() will create a pool with at least @nr_elems,
 * but possibily more depending on page alignment.
 *
 * There should be one datastore per C data type (in general).
 * Each core, flow-group or unit of concurrency will create a distinct mempool leveraging the datastore
 *
 * Returns 0 if successful, otherwise fail.
 */

//int mempool_create_datastore(struct mempool_datastore *mds, int nr_elems, size_t elem_len, int nostraddle, int chunk_size, const char *name)
int mempool_create_mbuf_datastore(struct mempool_datastore *mds, int nr_elems, size_t elem_len, const char *name)
{
	assert(mds->magic == 0);

	if (!elem_len || !nr_elems)
		return -EINVAL;

	mds->magic = MEMPOOL_MAGIC;
	mds->prettyname = name;
	elem_len = align_up(elem_len, sizeof(long)) + MEMPOOL_INITIAL_OFFSET;

	// Arguments for mempool constructor in case they need to be changed later
	unsigned cache_size = RTE_MEMPOOL_CACHE_MAX_SIZE;
	unsigned private_data_size = 0;
	rte_mempool_ctor_t* mp_init = rte_pktmbuf_pool_init;
	void* mp_init_arg = NULL;
	rte_mempool_obj_ctor_t* obj_init = rte_pktmbuf_init;
	void* obj_init_arg = NULL;
	int socket_id = rte_socket_id();
	unsigned flags = 0;
	mds->pool = rte_pktmbuf_pool_create(name, nr_elems, cache_size, private_data_size, elem_len, socket_id);
	//mds->pool = rte_mempool_create(name, nr_elems, elem_len, cache_size, private_data_size, mp_init, mp_init_arg, obj_init, obj_init_arg, socket_id, flags);

	
	mds->nr_elems = nr_elems;
	mds->elem_len = elem_len;

	if (mds->pool == NULL) {
		log_err("mempool mbuf alloc failed\n");
		printf("Unable to create mempool mbuf datastore %s | nr_elems: %d | elem_len: %lu | Error: %s\n", name, nr_elems, elem_len, rte_strerror(rte_errno));


		panic("unable to create mempool mbuf datastore for %s\n", name);
		return -ENOMEM;
	}

	mds->next_ds = mempool_all_datastores;
	mempool_all_datastores = mds;

	printf("mempool_mbuf_datastore: %-15s elem_len:%lu nr_elems:%d\n",
	       name,
	       mds->elem_len,
	       nr_elems);

	return 0;
}


/**
 * mempool_create - initializes a memory pool
 * @nr_elems: the minimum number of elements in the pool
 * @elem_len: the length of each element
 *
 * NOTE: mempool_create() will create a pool with at least @nr_elems,
 * but possibily more depending on page alignment.
 *
 * Returns 0 if successful, otherwise fail.
 */
int mempool_create(struct mempool *m, struct mempool_datastore *mds, int16_t sanity_type, int16_t sanity_id)
{

	if (mds->magic != MEMPOOL_MAGIC)
		panic("mempool_create when datastore does not exist\n");

	assert(mds->magic == MEMPOOL_MAGIC);

	if (m->magic != 0)
		panic("mempool_create attempt to call twice (ds=%s)\n", mds->prettyname);

	assert(m->magic == 0);
	m->magic = MEMPOOL_MAGIC;
	m->datastore = mds;
	m->sanity = (sanity_type << 16) | sanity_id;
	m->nr_elems = mds->nr_elems;
	m->elem_len = mds->elem_len;
	return 0;
}

/**
 * mempool_destroy - cleans up a memory pool and frees memory
 * @m: the memory pool
 */

void mempool_destroy_datastore(struct mempool_datastore *mds)
{
	mds->magic = 0;
	
	//FIXME: This version of DPDK doesn't have a mempool_free???
	//rte_mempool_free(mds->pool);
}


#ifdef ENABLE_KSTATS

#define PRINT_INTERVAL (5 * ONE_SECOND)
static void mempool_printstats(struct timer *t, struct eth_fg *cur_fg)
{
	struct mempool_datastore *mds = mempool_all_datastores;
	printf("DATASTORE name             free%% lock/s\n");

	for (; mds; mds = mds->next_ds)  {
		printf("DATASTORE %-15s", mds->prettyname);
	}
	timer_add(t, NULL, PRINT_INTERVAL);
}
#endif

int mempool_init(void)
{
#ifdef ENABLE_KSTATS
	timer_init_entry(&mempool_timer, mempool_printstats);
	timer_add(&mempool_timer, NULL, PRINT_INTERVAL);
#endif
	return 0;
}


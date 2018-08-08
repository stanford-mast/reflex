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
#include <rte_malloc.h>

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
	unsigned flags = MEMPOOL_F_SC_GET | MEMPOOL_F_SP_PUT; //NOTE: need to modify DPDK rte_mempool.h to use cache for SC/SP
    if(rte_eal_process_type() == RTE_PROC_PRIMARY) { 
	    mds->pool = rte_mempool_create(name, nr_elems, elem_len, cache_size, private_data_size, mp_init, mp_init_arg, obj_init, obj_init_arg, socket_id, flags);
    } else {
        mds->pool = rte_mempool_lookup(name);
    }
	
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

/* Free memory chunks used by a mempool. Objects must be in pool */
static void
rte_mempool_free_memchunks(struct rte_mempool *mp)
{
	struct rte_mempool_memhdr *memhdr;
	void *elt;

	while (!STAILQ_EMPTY(&mp->elt_list)) {
		rte_mempool_ops_dequeue_bulk(mp, &elt, 1);
		(void)elt;
		STAILQ_REMOVE_HEAD(&mp->elt_list, next);
		mp->populated_size--;
	}

	while (!STAILQ_EMPTY(&mp->mem_list)) {
		memhdr = STAILQ_FIRST(&mp->mem_list);
		STAILQ_REMOVE_HEAD(&mp->mem_list, next);
		if (memhdr->free_cb != NULL)
			memhdr->free_cb(memhdr, memhdr->opaque);
		rte_free(memhdr);
		mp->nb_mem_chunks--;
	}
}

/* free a memchunk allocated with rte_memzone_reserve() */
static void
rte_mempool_memchunk_mz_free(__rte_unused struct rte_mempool_memhdr *memhdr,
	void *opaque)
{
	const struct rte_memzone *mz = opaque;
	rte_memzone_free(mz);
}


static void
mempool_add_elem(struct rte_mempool *mp, void *obj, phys_addr_t physaddr)
{
	struct rte_mempool_objhdr *hdr;
	struct rte_mempool_objtlr *tlr __rte_unused;

	/* set mempool ptr in header */
	hdr = RTE_PTR_SUB(obj, sizeof(*hdr));
	hdr->mp = mp;
	hdr->physaddr = physaddr;
	STAILQ_INSERT_TAIL(&mp->elt_list, hdr, next);
	mp->populated_size++;

	/* enqueue in ring */
	rte_mempool_ops_enqueue_bulk(mp, &obj, 1);
}


int
rte_mempool_populate_phys_align(struct rte_mempool *mp, char *vaddr,
	phys_addr_t paddr, size_t len, rte_mempool_memchunk_free_cb_t *free_cb,
	void *opaque)
{
	unsigned total_elt_sz;
	unsigned i = 0;
	size_t off;
	struct rte_mempool_memhdr *memhdr;
	int ret;

	/* create the internal ring if not already done */
	if ((mp->flags & MEMPOOL_F_POOL_CREATED) == 0) {
		ret = rte_mempool_ops_alloc(mp);
		if (ret != 0)
			return ret;
		mp->flags |= MEMPOOL_F_POOL_CREATED;
	}

	/* mempool is already populated */
	if (mp->populated_size >= mp->size)
		return -ENOSPC;

	mp->header_size = 0; //NEW
	total_elt_sz = mp->header_size + mp->elt_size + mp->trailer_size;

	memhdr = rte_zmalloc("MEMPOOL_MEMHDR", sizeof(*memhdr), 0);
	if (memhdr == NULL)
		return -ENOMEM;

	printf("memhdr is %p\n", memhdr);

	memhdr->mp = mp;
	memhdr->addr = vaddr;
	memhdr->phys_addr = paddr;
	memhdr->len = len;
	memhdr->free_cb = free_cb;
	memhdr->opaque = opaque;

	if (mp->flags & MEMPOOL_F_NO_CACHE_ALIGN)
		off = RTE_PTR_ALIGN_CEIL(vaddr, 8) - vaddr;
	else{
		off = RTE_PTR_ALIGN_CEIL(vaddr, RTE_CACHE_LINE_SIZE) - vaddr;
		off = RTE_PTR_ALIGN_CEIL(vaddr, 4096) - vaddr; //NEW
	}

	while (off + total_elt_sz <= len && mp->populated_size < mp->size) {
		off += mp->header_size;
		if (paddr == RTE_BAD_PHYS_ADDR)
			mempool_add_elem(mp, (char *)vaddr + off,
				RTE_BAD_PHYS_ADDR);
		else
			mempool_add_elem(mp, (char *)vaddr + off, paddr + off);
		off += mp->elt_size + mp->trailer_size;
		off = RTE_ALIGN_CEIL(off, 4096);  //NEW
		i++;
	}

	/* not enough room to store one object */
	if (i == 0)
		return -EINVAL;

	STAILQ_INSERT_TAIL(&mp->mem_list, memhdr, next);
	mp->nb_mem_chunks++;
	return i;
}


int
rte_mempool_populate_align(struct rte_mempool *mp)
{
	int mz_flags = RTE_MEMZONE_1GB|RTE_MEMZONE_SIZE_HINT_ONLY;
	char mz_name[RTE_MEMZONE_NAMESIZE];
	const struct rte_memzone *mz;
	size_t size, total_elt_sz, align, pg_sz, pg_shift;
	phys_addr_t paddr;
	unsigned mz_id, n;
	int ret;

	/* mempool must not be populated */
	if (mp->nb_mem_chunks != 0)
		return -EEXIST;

	if (rte_eal_has_hugepages()) {
		pg_shift = 0; /* not needed, zone is physically contiguous */
		pg_sz = 0;
		//align = RTE_CACHE_LINE_SIZE;
	} else {
		pg_sz = getpagesize();
		pg_shift = rte_bsf32(pg_sz);
		//align = pg_sz;
	}

	align = 4096 ; //NEW!!

	total_elt_sz = mp->header_size + mp->elt_size + mp->trailer_size;
	for (mz_id = 0, n = mp->size; n > 0; mz_id++, n -= ret) {
		size = rte_mempool_xmem_size(n, total_elt_sz, pg_shift);

		ret = snprintf(mz_name, sizeof(mz_name),
			RTE_MEMPOOL_MZ_FORMAT "_%d", mp->name, mz_id);
		if (ret < 0 || ret >= (int)sizeof(mz_name)) {
			ret = -ENAMETOOLONG;
			goto fail;
		}

		mz = rte_memzone_reserve_aligned(mz_name, size,
			mp->socket_id, mz_flags, align);
		/* not enough memory, retry with the biggest zone we have */
		if (mz == NULL)
			mz = rte_memzone_reserve_aligned(mz_name, 0,
				mp->socket_id, mz_flags, align);
		if (mz == NULL) {
			ret = -rte_errno;
			goto fail;
		}

		if (mp->flags & MEMPOOL_F_NO_PHYS_CONTIG)
			paddr = RTE_BAD_PHYS_ADDR;
		else
			paddr = mz->phys_addr;


		printf("call mempool_populate_phys_align for %p\n", mp);
		ret = rte_mempool_populate_phys_align(mp, mz->addr,
											  paddr, mz->len,
											  rte_mempool_memchunk_mz_free,
											  (void *)(uintptr_t)mz);
		
		if (ret < 0) {
			rte_memzone_free(mz);
			goto fail;
		}
	}

	return mp->size;

 fail:
	rte_mempool_free_memchunks(mp);
	return ret;
}



/* 
 * Create a 4KB-aligned mempool
 * based on DPDK but no header in mempool object so objects can be 4KB aligned 
 * 4kB aligned is required to pass these objects as req payload to SPDK
 *
 */
struct rte_mempool *
rte_mempool_create_align(const char *name, unsigned n, unsigned elt_size,
	unsigned cache_size, unsigned private_data_size,
	rte_mempool_ctor_t *mp_init, void *mp_init_arg,
	rte_mempool_obj_cb_t *obj_init, void *obj_init_arg,
	int socket_id, unsigned flags)
{
	struct rte_mempool *mp;
	int ret;

	if(rte_eal_process_type() == RTE_PROC_PRIMARY) { 
        mp = rte_mempool_create_empty(name, n, elt_size, cache_size,
		    private_data_size, socket_id, flags);
    } else {
        mp = rte_mempool_lookup(name);
    }


	if (mp == NULL)
		return NULL;

    if(rte_eal_process_type() == RTE_PROC_PRIMARY) { 
	    ret = rte_mempool_set_ops_byname(mp, "ring_sp_sc", NULL);


	    if (ret) {
		    goto fail;
        }
    }

	/* call the mempool priv initializer */
	if (mp_init)
		mp_init(mp, mp_init_arg);
    
	if (rte_eal_process_type() == RTE_PROC_PRIMARY && rte_mempool_populate_align(mp) < 0) {
		goto fail;
    }

	/* call the object initializers */
	if (obj_init)
		rte_mempool_obj_iter(mp, obj_init, obj_init_arg);

	return mp;

 fail:
	printf("ERROR: cannto initalize aligned mempool\n");
	rte_mempool_free(mp);
	return NULL;
}



int mempool_create_datastore_align(struct mempool_datastore *mds, int nr_elems, size_t elem_len, const char *name)
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
	rte_mempool_ctor_t* mp_init = NULL;
	void* mp_init_arg = NULL;
	rte_mempool_obj_ctor_t* obj_init = NULL;
	void* obj_init_arg = NULL;
	int socket_id = rte_socket_id();
	unsigned flags = MEMPOOL_F_SC_GET | MEMPOOL_F_SP_PUT;


	mds->pool = rte_mempool_create_align(name, nr_elems, elem_len, cache_size, private_data_size, mp_init, mp_init_arg, obj_init, obj_init_arg, socket_id, flags);
	
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
	unsigned flags = MEMPOOL_F_SC_GET | MEMPOOL_F_SP_PUT;
    if(rte_eal_process_type() == RTE_PROC_PRIMARY) { 
	    mds->pool = rte_pktmbuf_pool_create(name, nr_elems, cache_size, private_data_size, elem_len, socket_id);
    } else {
        mds->pool = rte_mempool_lookup(name);
    }

	
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

int mempool_create_contig_nopad(struct mempool *m, struct mempool_datastore *mds, int16_t sanity_type, int16_t sanity_id)
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
	
	rte_mempool_free(mds->pool);
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




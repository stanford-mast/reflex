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
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
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

/* 
 * A DPDK-based mempool but with no padding 
 * so objects are fully contiguous in physical memory
 * This is required for NVMe PRP list 
 * 
 */

#include <ix/mempool.h>
#include <rte_config.h>
#include <rte_mempool.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_eal_memconfig.h>
#include <rte_errno.h>

static struct mempool_datastore *mempool_all_contig_datastores;

/*
static struct rte_tailq_elem rte_mempool_tailq_contig = {
	.name = "RTE_MEMPOOL",
};
EAL_REGISTER_TAILQ(rte_mempool_tailq_contig)
*/
#define CACHE_FLUSHTHRESH_MULTIPLIER 1.5
#define CALC_CACHE_FLUSHTHRESH(c)	\
	((typeof(c))((c) * CACHE_FLUSHTHRESH_MULTIPLIER))

/*
 * Populate  mempool with the objects.
 */

struct mempool_populate_arg {
	struct rte_mempool     *mp;
	rte_mempool_obj_ctor_t *obj_init;
	void                   *obj_init_arg;
};

static void
mempool_add_elem(struct rte_mempool *mp, void *obj, uint32_t obj_idx,
	rte_mempool_obj_ctor_t *obj_init, void *obj_init_arg)
{
	struct rte_mempool_objhdr *hdr __rte_unused;
	struct rte_mempool_objtlr *tlr __rte_unused;

	obj = (char *)obj + mp->header_size;

	//MODIFIED:  
	/* set mempool ptr in header */
	//hdr = RTE_PTR_SUB(obj, sizeof(*hdr));
	//hdr->mp = mp; 

#ifdef RTE_LIBRTE_MEMPOOL_DEBUG
	hdr->cookie = RTE_MEMPOOL_HEADER_COOKIE2;
	tlr = __mempool_get_trailer(obj);
	tlr->cookie = RTE_MEMPOOL_TRAILER_COOKIE;
#endif
	/* call the initializer */
	if (obj_init)
		obj_init(mp, obj_init_arg, obj, obj_idx);

	/* enqueue in ring */
	rte_ring_sp_enqueue(mp->ring, obj);
}

static void
mempool_obj_populate(void *arg, void *start, void *end, uint32_t idx)
{
	struct mempool_populate_arg *pa = arg;

	mempool_add_elem(pa->mp, start, idx, pa->obj_init, pa->obj_init_arg);
	pa->mp->elt_va_end = (uintptr_t)end;
}

static void
mempool_populate(struct rte_mempool *mp, size_t num, size_t align,
	rte_mempool_obj_ctor_t *obj_init, void *obj_init_arg)
{
	uint32_t elt_sz;
	struct mempool_populate_arg arg;

	elt_sz = mp->elt_size + mp->header_size + mp->trailer_size;
	arg.mp = mp;
	arg.obj_init = obj_init;
	arg.obj_init_arg = obj_init_arg;

	printf("mempool_populating...align is %ld, num is %ld, elt_sz is %ld\n", align, num, elt_sz);
	mp->size = rte_mempool_obj_iter((void *)mp->elt_va_start,
		num, elt_sz, align,
		mp->elt_pa, mp->pg_num, mp->pg_shift,
		mempool_obj_populate, &arg);
}

struct rte_mempool *
rte_mempool_create_contig_nopad(const char *name, unsigned n, unsigned elt_size,
		unsigned cache_size, unsigned private_data_size,
		rte_mempool_ctor_t *mp_init, void *mp_init_arg,
		rte_mempool_obj_ctor_t *obj_init, void *obj_init_arg,
		int socket_id, unsigned flags, void *vaddr,
		const phys_addr_t paddr[], uint32_t pg_num, uint32_t pg_shift)
{
	char mz_name[RTE_MEMZONE_NAMESIZE];
	char rg_name[RTE_RING_NAMESIZE];
//	struct rte_mempool_list *mempool_list; //MODIFIED
	struct rte_mempool *mp = NULL;
	struct rte_tailq_entry *te = NULL;
	struct rte_ring *r = NULL;
	const struct rte_memzone *mz;
	size_t mempool_size;
	int mz_flags = RTE_MEMZONE_1GB|RTE_MEMZONE_SIZE_HINT_ONLY;
	int rg_flags = 0;
	void *obj;
	struct rte_mempool_objsz objsz;
	void *startaddr;
	int page_size = getpagesize();

	/* compilation-time checks */
	RTE_BUILD_BUG_ON((sizeof(struct rte_mempool) &
			  RTE_CACHE_LINE_MASK) != 0);
#if RTE_MEMPOOL_CACHE_MAX_SIZE > 0
	RTE_BUILD_BUG_ON((sizeof(struct rte_mempool_cache) &
			  RTE_CACHE_LINE_MASK) != 0);
	RTE_BUILD_BUG_ON((offsetof(struct rte_mempool, local_cache) &
			  RTE_CACHE_LINE_MASK) != 0);
#endif
#ifdef RTE_LIBRTE_MEMPOOL_DEBUG
	RTE_BUILD_BUG_ON((sizeof(struct rte_mempool_debug_stats) &
			  RTE_CACHE_LINE_MASK) != 0);
	RTE_BUILD_BUG_ON((offsetof(struct rte_mempool, stats) &
			  RTE_CACHE_LINE_MASK) != 0);
#endif

//	mempool_list = RTE_TAILQ_CAST(rte_mempool_tailq.head, rte_mempool_list); //MODIFIED

	/* asked cache too big */
	if (cache_size > RTE_MEMPOOL_CACHE_MAX_SIZE ||
	    CALC_CACHE_FLUSHTHRESH(cache_size) > n) {
		rte_errno = EINVAL;
		return NULL;
	}

	/* check that we have both VA and PA */
	if (vaddr != NULL && paddr == NULL) {
		rte_errno = EINVAL;
		return NULL;
	}

	/* Check that pg_num and pg_shift parameters are valid. */
	if (pg_num < RTE_DIM(mp->elt_pa) || pg_shift > MEMPOOL_PG_SHIFT_MAX) {
		rte_errno = EINVAL;
		return NULL;
	}

	/* "no cache align" imply "no spread" */
	if (flags & MEMPOOL_F_NO_CACHE_ALIGN)
		flags |= MEMPOOL_F_NO_SPREAD;

	/* ring flags */
	if (flags & MEMPOOL_F_SP_PUT)
		rg_flags |= RING_F_SP_ENQ;
	if (flags & MEMPOOL_F_SC_GET)
		rg_flags |= RING_F_SC_DEQ;

	/* calculate mempool object sizes. */
/*	if (!rte_mempool_calc_obj_size(elt_size, flags, &objsz)) {
		rte_errno = EINVAL;
		return NULL;
	}
*/
	rte_rwlock_write_lock(RTE_EAL_MEMPOOL_RWLOCK);

	/* allocate the ring that will be used to store objects */
	/* Ring functions will return appropriate errors if we are
	 * running as a secondary process etc., so no checks made
	 * in this function for that condition */
	snprintf(rg_name, sizeof(rg_name), RTE_MEMPOOL_MZ_FORMAT, name);
	r = rte_ring_create(rg_name, rte_align32pow2(n+1), socket_id, rg_flags);
	if (r == NULL)
		goto exit_unlock;

	/*
	 * reserve a memory zone for this mempool: private data is
	 * cache-aligned
	 */
	private_data_size = (private_data_size +
			     RTE_MEMPOOL_ALIGN_MASK) & (~RTE_MEMPOOL_ALIGN_MASK);

	if (! rte_eal_has_hugepages()) {
		/*
		 * expand private data size to a whole page, so that the
		 * first pool element will start on a new standard page
		 */
		int head = sizeof(struct rte_mempool);
		int new_size = (private_data_size + head) % page_size;
		if (new_size) {
			private_data_size += page_size - new_size;
		}
	}

	/* try to allocate tailq entry */
	/*
	te = rte_zmalloc("MEMPOOL_TAILQ_ENTRY", sizeof(*te), 0);
	if (te == NULL) {
		RTE_LOG(ERR, MEMPOOL, "Cannot allocate tailq entry!\n");
		goto exit_unlock;
	}
*/
	/*
	 * If user provided an external memory buffer, then use it to
	 * store mempool objects. Otherwise reserve a memzone that is large
	 * enough to hold mempool header and metadata plus mempool objects.
	 */
	mempool_size = MEMPOOL_HEADER_SIZE(mp, pg_num) + private_data_size;
	mempool_size = RTE_ALIGN_CEIL(mempool_size, RTE_MEMPOOL_ALIGN);
	if (vaddr == NULL){
		mempool_size += (size_t)elt_size * n; //(size_t)objsz.total_size * n;
	}

	if (! rte_eal_has_hugepages()) {
		/*
		 * we want the memory pool to start on a page boundary,
		 * because pool elements crossing page boundaries would
		 * result in discontiguous physical addresses
		 */
		mempool_size += page_size;
	}

	snprintf(mz_name, sizeof(mz_name), RTE_MEMPOOL_MZ_FORMAT, name);

	mz = rte_memzone_reserve(mz_name, mempool_size, socket_id, mz_flags);
	if (mz == NULL)
		goto exit_unlock;

	if (rte_eal_has_hugepages()) {
		startaddr = (void*)mz->addr;
	} else {
		/* align memory pool start address on a page boundary */
		unsigned long addr = (unsigned long)mz->addr;
		if (addr & (page_size - 1)) {
			addr += page_size;
			addr &= ~(page_size - 1);
		}
		startaddr = (void*)addr;
	}

	/* init the mempool structure */
	mp = startaddr;
	memset(mp, 0, sizeof(*mp));
	snprintf(mp->name, sizeof(mp->name), "%s", name);
	mp->phys_addr = mz->phys_addr;
	mp->ring = r;
	mp->size = n;
	mp->flags = flags;
	mp->elt_size = elt_size; //objsz.elt_size;
	mp->header_size = 0; // objsz.header_size;
	mp->trailer_size = 0; //objsz.trailer_size;
	mp->cache_size = cache_size;
	mp->cache_flushthresh = CALC_CACHE_FLUSHTHRESH(cache_size);
	mp->private_data_size = private_data_size;

	/* calculate address of the first element for continuous mempool. */
	obj = (char *)mp + MEMPOOL_HEADER_SIZE(mp, pg_num) +
		private_data_size;
	obj = RTE_PTR_ALIGN_CEIL(obj, RTE_MEMPOOL_ALIGN);

	/* populate address translation fields. */
	mp->pg_num = pg_num;
	mp->pg_shift = pg_shift;
	mp->pg_mask = RTE_LEN2MASK(mp->pg_shift, typeof(mp->pg_mask));

	/* mempool elements allocated together with mempool */
	if (vaddr == NULL) {
		mp->elt_va_start = (uintptr_t)obj;
		mp->elt_pa[0] = mp->phys_addr +
			(mp->elt_va_start - (uintptr_t)mp);

	/* mempool elements in a separate chunk of memory. */
	} else {
		mp->elt_va_start = (uintptr_t)vaddr;
		memcpy(mp->elt_pa, paddr, sizeof (mp->elt_pa[0]) * pg_num);
	}

	mp->elt_va_end = mp->elt_va_start;

	/* call the initializer */
	if (mp_init)
		mp_init(mp, mp_init_arg);

	//mempool_populate(mp, n, 1, obj_init, obj_init_arg);
	mempool_populate(mp, n, 4096, obj_init, obj_init_arg); //MODIFIED

	//te->data = (void *) mp;

	/* //FIXME: do we need to add to global mempool list?
	rte_rwlock_write_lock(RTE_EAL_TAILQ_RWLOCK);
	TAILQ_INSERT_TAIL(mempool_list, te, next);
	rte_rwlock_write_unlock(RTE_EAL_TAILQ_RWLOCK);
	rte_rwlock_write_unlock(RTE_EAL_MEMPOOL_RWLOCK);
	*/
	return mp;

exit_unlock:
	//rte_rwlock_write_unlock(RTE_EAL_MEMPOOL_RWLOCK);
	rte_ring_free(r);
	rte_free(te);

	return NULL;
}


int mempool_create_datastore_contig_nopad(struct mempool_datastore *mds, int nr_elems, size_t elem_len, const char *name)
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
	unsigned flags = MEMPOOL_F_NO_SPREAD | MEMPOOL_F_SC_GET | MEMPOOL_F_SP_PUT;

	mds->pool = rte_mempool_create_contig_nopad(name, nr_elems, elem_len,
					       cache_size, private_data_size,
					       mp_init, mp_init_arg,
					       obj_init, obj_init_arg,
					       socket_id, flags,
					       NULL, NULL, MEMPOOL_PG_NUM_DEFAULT,
					       MEMPOOL_PG_SHIFT_MAX);
	
	mds->nr_elems = nr_elems;
	mds->elem_len = elem_len;
	
	if (mds->pool == NULL) {
		log_err("mempool alloc failed\n");
		printf("Unable to create mempool datastore %s | nr_elems: %d | elem_len: %lu | Error: %s\n", name, nr_elems, elem_len, rte_strerror(rte_errno));


		panic("unable to create mempool datastore for %s\n", name);
		return -ENOMEM;
	}

	mds->next_ds = mempool_all_contig_datastores;
	mempool_all_contig_datastores = mds;

	printf("mempool_datastore: %-15s elem_len:%lu nr_elems:%d\n",
	       name,
	       mds->elem_len,
	       nr_elems);

	return 0;
}

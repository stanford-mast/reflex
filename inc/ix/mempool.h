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
 * mempool.h - a fast fixed-sized memory pool allocator
 */

#pragma once

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_mempool.h>

#include <ix/stddef.h>
#include <assert.h>
#include <ix/cpu.h>
#include <ix/ethfg.h>
#include <ix/log.h>



#define MEMPOOL_DEFAULT_CHUNKSIZE 128


#undef  DEBUG_MEMPOOL

#ifdef DEBUG_MEMPOOL
#define MEMPOOL_INITIAL_OFFSET (sizeof(void *))
#else
#define MEMPOOL_INITIAL_OFFSET (0)
#endif

// one per data type
struct mempool_datastore {
	uint64_t                 magic;
	spinlock_t               lock;
	struct rte_mempool	*pool;
	uint32_t                nr_elems;
	size_t                  elem_len;
	int64_t                 num_locks;
	const char             *prettyname;
	struct mempool_datastore *next_ds;
};


struct mempool {
	int                     num_free;
	size_t                  elem_len;

	uint64_t                 magic;
	struct mempool_datastore *datastore;
	int                     sanity;
	uint32_t                nr_elems;
};
#define MEMPOOL_MAGIC   0x12911776


/*
 * mempool sanity macros ensures that pointers between mempool-allocated objects are of identical type
 */

#define MEMPOOL_SANITY_GLOBAL    0
#define MEMPOOL_SANITY_PERCPU    1

#ifdef DEBUG_MEMPOOL


#define MEMPOOL_SANITY_OBJECT(_a) do {\
	struct mempool **hidden = (struct mempool **)_a;\
	assert(hidden[-1] && hidden[-1]->magic == MEMPOOL_MAGIC); \
	} while (0);

static inline int __mempool_get_sanity(void *a)
{
	struct mempool **hidden = (struct mempool **)a;
	struct mempool *p = hidden[-1];
	return p->sanity;
}


#define MEMPOOL_SANITY_ACCESS(_obj)   do { \
	MEMPOOL_SANITY_OBJECT(_obj);\
	} while (0);

#define  MEMPOOL_SANITY_LINK(_a, _b) do {\
	MEMPOOL_SANITY_ACCESS(_a);\
	MEMPOOL_SANITY_ACCESS(_b);\
	int sa = __mempool_get_sanity(_a);\
	int sb = __mempool_get_sanity(_b);\
	assert (sa == sb);\
	}  while (0);

#else
#define MEMPOOL_SANITY_ISPERFG(_a)
#define MEMPOOL_SANITY_ACCESS(_a)
#define MEMPOOL_SANITY_LINK(_a, _b)
#endif


/**
 * mempool_alloc - allocates an element from a memory pool
 * @m: the memory pool
 *
 * Returns a pointer to the allocated element or NULL if unsuccessful.
 */
extern void *mempool_alloc_2(struct mempool *m);
static inline void *mempool_alloc(struct mempool *m)
{
	// PTR to fill form mempool
	void *ptr;
	
	// Get memory from mempool 
	int ret = rte_mempool_get(m->datastore->pool, &ptr);
	if (ret){
		log_debug("rte_mempool_get returned error %d\n", ret);
		return NULL;
	}
	else
		return ptr;

}

/**
 * mempool_free - frees an element back in to a memory pool
 * @m: the memory pool
 * @ptr: the element
 *
 * NOTE: Must be the same memory pool that it was allocated from
 */
extern void mempool_free_2(struct mempool *m, void *ptr);
static inline void mempool_free(struct mempool *m, void *ptr)
{

	// Put memory back into mempool 
	rte_mempool_put(m->datastore->pool, ptr);

}

static inline void *mempool_idx_to_ptr(struct mempool *m, uint32_t idx, int elem_len)
{
	void *p;
	assert(idx < m->nr_elems);
	p= m->datastore + elem_len * idx + MEMPOOL_INITIAL_OFFSET;
	MEMPOOL_SANITY_ACCESS(p);
	return p;
}

static inline uintptr_t mempool_ptr_to_idx(struct mempool *m, void *p, int elem_len)
{
	uintptr_t x = (uintptr_t)p - (uintptr_t)m->datastore->pool - MEMPOOL_INITIAL_OFFSET;
	x = x / elem_len;
	assert(x < m->nr_elems);
	return x;
}


extern int mempool_create_datastore(struct mempool_datastore *m, int nr_elems, size_t elem_len, const char *prettyname);
extern int mempool_create_datastore_align(struct mempool_datastore *m, int nr_elems, size_t elem_len, const char *prettyname);
extern int mempool_create_datastore_contig_nopad(struct mempool_datastore *m, int nr_elems, size_t elem_len, const char *prettyname);
extern int mempool_create(struct mempool *m, struct mempool_datastore *mds, int16_t sanity_type, int16_t sanity_id);
extern void mempool_destroy(struct mempool *m);




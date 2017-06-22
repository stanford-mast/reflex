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
 * mbuf.c - buffer management for network packets
 *
 * TODO: add support for mapping into user-level address space...
 */

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_per_lcore.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <ix/stddef.h>
#include <ix/mempool.h>
#include <ix/mbuf.h>
#include <ix/cpu.h>

/* Capacity should be at least RX queues per CPU * ETH_DEV_RX_QUEUE_SZ */
#define MBUF_CAPACITY	20480 /* Originally set to (768*1024), but decrease # of mbufs allocated and use memory to create larger mbufs for jumbo frames */

static struct mempool_datastore mbuf_datastore;

RTE_DEFINE_PER_LCORE(struct mempool, mbuf_mempool __attribute__((aligned(64))));

void mbuf_default_done(struct mbuf *m)
{
	mbuf_free(m);
}

/**
 * mbuf_init_cpu - allocates the core-local mbuf region
 *
 * Returns 0 if successful, otherwise failure.
 */

int mbuf_init_cpu(void)
{
	struct mempool *m = &percpu_get(mbuf_mempool);
	return mempool_create(m, &mbuf_datastore, MEMPOOL_SANITY_PERCPU, percpu_get(cpu_id));
}

/**
 * mbuf_init - allocate global mbuf
 */

int mbuf_init(void)
{
	int ret;
	struct mempool_datastore *m = &mbuf_datastore;
	ret = mempool_create_mbuf_datastore(m, MBUF_CAPACITY, DPDK_MBUF_SIZE, "mbuf");
	if (ret) {
		assert(0);
		return ret;
	}
	//ret = mempool_pagemem_map_to_user(m);
	if (ret) {
		assert(0);
		//mempool_pagemem_destroy(m);
		mempool_destroy_datastore(m);
		return ret;
	}
	
	return 0;
}

/**
 * mbuf_exit_cpu - frees the core-local mbuf region
 */
void mbuf_exit_cpu(void)
{
	mempool_destroy_datastore(&mbuf_datastore);
}


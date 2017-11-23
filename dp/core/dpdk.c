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

/* For memmove and size_t */
#include <string.h>

/* For optind */
#include <unistd.h>

/* For struct sockaddr */
#include <sys/socket.h>

/* Prevent inclusion of rte_memcpy.h */
//#define _RTE_MEMCPY_X86_64_H_
//inline void *rte_memcpy(void *dst, const void *src, size_t n);

/* General DPDK includes */
#include <rte_config.h>
#include <rte_common.h>
#include <rte_mempool.h>
#include <eal_internal_cfg.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

/* IX includes */
#include <ix/log.h>
#include <ix/dpdk.h>
#include <ix/cfg.h>

#include <spdk/env.h>
#include <math.h>

struct rte_mempool *dpdk_pool;

#define MEMPOOL_CACHE_SIZE 256
#define DPDK_MBUF_LENGTH 9000 + RTE_PKTMBUF_HEADROOM

int dpdk_init(void)
{
	struct spdk_env_opts opts;
        
	spdk_env_opts_init(&opts);
        opts.name = "reflex";
        opts.shm_id = -1;	

	int core_num = pow(2, cores_active)-1;
	char mask[3];
	sprintf(mask, "%x", core_num);
	opts.core_mask = mask;
       
        spdk_env_init(&opts);	

	/* pool_size sets an implicit limit on cores * NICs that DPDK allows */
	const int pool_size = 32768;

	//dpdk_pool = rte_pktmbuf_pool_create("mempool", pool_size, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	dpdk_pool = rte_pktmbuf_pool_create("mempool", pool_size, MEMPOOL_CACHE_SIZE, 0, DPDK_MBUF_LENGTH, rte_socket_id());
	if (dpdk_pool == NULL)
		panic("Cannot create DPDK pool\n");

	return 0;
}


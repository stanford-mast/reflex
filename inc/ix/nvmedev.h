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
 * nvmedev.h - IX support for nvme interface
 */

#pragma once

#include <rte_per_lcore.h>

#include <ix/bitmap.h>
#include <ix/syscall.h>
#include <ix/list.h>

/* FIXME: this should be read from NVMe device register */
#define MAX_NUM_IO_QUEUES 31

#define NVME_CMD_READ 0
#define NVME_CMD_WRITE 1


#define NVME_MAX_COMPLETIONS 64

#define MAX_NVME_FLOW_GROUPS 16384 //16
DEFINE_BITMAP(ioq_bitmap, MAX_NUM_IO_QUEUES);
DEFINE_BITMAP(nvme_fgs_bitmap, MAX_NVME_FLOW_GROUPS);
RTE_DECLARE_PER_LCORE(struct spdk_nvme_qpair *, qpair);


struct nvme_ctx {
	hqu_t handle;
	unsigned long cookie;
	union user_buf {
		void * buf;
		struct sgl_buf{
			void **sgl;
			int num_sgls;
			int current_sgl;
		} sgl_buf;
	} user_buf;
	// added for SW scheduling...
	unsigned int tid; 				//thread id = percpu_get(cpu_nr)
	//hqu_t priority;					//request priority (determined by flow priority)
	hqu_t fg_handle;					//flow group handle 
	int cmd; 						//NVME_CMD_[READ or WRITE]
	int req_cost; 					//cost of request in tokens
	// command arguments...
	struct spdk_nvme_ns *ns;		//namespace
	void* paddr;					//physical addr of buffer to write/read to
	unsigned long lba;				//logical block address
	unsigned int lba_count;			//size of IO in logical blocks
	const struct nvme_completion* completion;	//callback function handle
	unsigned long time;
};


struct nvme_flow_group {
	int flow_group_id;				// flow group id (index in bitmap)
	//long ns_id; 					// namespace id
	unsigned long cookie;			// cookie associated with connection context for user
	unsigned int latency_us_SLO;	// latency SLO info (0 if best effort)
	unsigned long IOPS_SLO;
	int rw_ratio_SLO;
	unsigned long scaled_IOPS_limit; // calculated based on IOPS, rw_ratio and rw cost
	double scaled_IOPuS_limit; 		
	bool latency_critical_flag;
	struct nvme_sw_queue* nvme_swq;	// thread-local software queue for this flow group
	unsigned int tid; 				// thread id 
	int conn_ref_count;
};

struct nvme_tenant_mgmt {
	struct list_head tenant_swq;
	int num_tenants;
	int num_best_effort_tenants;
};

/*
struct nvme_tenant {
	long fg_handle;
	struct list_node list;
};
*/

RTE_DECLARE_PER_LCORE(struct mempool, ctx_mempool);

RTE_DECLARE_PER_LCORE(int, received_nvme_completions);

RTE_DECLARE_PER_LCORE(struct nvme_tenant_mgmt, nvme_tenant_manager);

extern struct nvme_ctx * alloc_local_nvme_ctx(void);
extern void free_local_nvme_ctx(struct nvme_ctx *req);
extern void nvme_process_completions(void); 
extern bool nvme_poll_completions(int max_completions);
extern int nvme_schedule(void);
extern int nvme_sched(void);


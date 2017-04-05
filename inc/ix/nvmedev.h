/*
 * nvmedev.h - IX support for nvme interface
 */

#pragma once

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
DECLARE_PERCPU(struct spdk_nvme_qpair *, qpair);


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

DECLARE_PERCPU(struct mempool, ctx_mempool);

DECLARE_PERCPU(int, received_nvme_completions);

DECLARE_PERCPU(struct nvme_tenant_mgmt, nvme_tenant_manager);

extern struct nvme_ctx * alloc_local_nvme_ctx(void);
extern void free_local_nvme_ctx(struct nvme_ctx *req);
extern void nvme_process_completions(void); 
extern bool nvme_poll_completions(int max_completions);
extern int nvme_schedule(void);
extern int nvme_sched(void);


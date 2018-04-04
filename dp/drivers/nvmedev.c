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

#define _GNU_SOURCE

#include <sys/socket.h>
#include <rte_per_lcore.h>

#include <ix/cfg.h>
#include <ix/errno.h>
#include <ix/log.h>
#include <ix/syscall.h>
#include <ix/nvmedev.h>
#include <ix/mempool.h>
#include <ix/nvme_sw_queue.h>
#include <ix/atomic.h>

#include <spdk/nvme.h>
#include <spdk/fd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <limits.h>

//#include <libaio.h>

static struct spdk_nvme_ctrlr *nvme_ctrlr = NULL;
static long global_ns_id = 1;
static long global_ns_size = 512*1048576; //FIXME: default device size in bytes; 
										  // for NVMe devices, ns size is read from device
										  // for non-NVMe device, stays at this default so set it to capacity of device
static long global_ns_sector_size = 1;
struct pci_dev *g_nvme_dev;

static int fd;

#define MAX_OPEN_BATCH 32 
#define NUM_NVME_REQUESTS (4096 * 256)//4096 * 64 //1024
#define SGL_PAGE_SIZE 4096 	//should match PAGE_SIZE defined in dp/core/reflex_server.c

RTE_DEFINE_PER_LCORE(int, open_ev[MAX_OPEN_BATCH]);
RTE_DEFINE_PER_LCORE(int, open_ev_ptr);
RTE_DEFINE_PER_LCORE(struct spdk_nvme_qpair *, qpair);
RTE_DEFINE_PER_LCORE(bool, mempool_initialized);

static DEFINE_SPINLOCK(nvme_bitmap_lock);

static struct mempool_datastore request_datastore;
static struct mempool_datastore ctx_datastore;
static struct mempool_datastore nvme_swq_datastore;

static struct nvme_flow_group nvme_fgs[MAX_NVME_FLOW_GROUPS];
static unsigned long global_token_rate = UINT_MAX; 				 	// max token rate device can handle for current strictest latency SLO
static atomic_u64_t global_leftover_tokens = ATOMIC_INIT(0); 	 	// shared token bucket
static unsigned long global_LC_sum_token_rate = 0; 	 				// LC tenant token reservation summed across all LC tenants globally
static unsigned long global_num_best_effort_tenants = 0; 			// total num of best effort tenants
static unsigned long global_num_lc_tenants = 0; 					// total num of latency critical tenants
static atomic_t global_be_token_rate_per_tenant = ATOMIC_INIT(0); 	// token rate per best effort tenant
static unsigned long global_lc_boost_no_BE = 0; 			 		// fair share of leftover tokens that LC tenant can use when no BE registered

#define MAX_NUM_THREADS 24
static int scheduled_bit_vector[MAX_NUM_THREADS];

#define TOKEN_FRAC_GIVEAWAY 0.9 
static long TOKEN_DEFICIT_LIMIT = 10000;
static bool global_readonly_flag = true;

#define SLO_REQ_SIZE 4096

#define MAX_EVENTS 100

RTE_DEFINE_PER_LCORE(struct mempool, request_mempool __attribute__ ((aligned (64))));
RTE_DEFINE_PER_LCORE(struct mempool, ctx_mempool __attribute__ ((aligned (64))));
RTE_DEFINE_PER_LCORE(struct mempool, nvme_swq_mempool __attribute__ ((aligned (64))));
RTE_DEFINE_PER_LCORE(int, received_nvme_completions);

RTE_DEFINE_PER_LCORE(struct nvme_tenant_mgmt, nvme_tenant_manager);

RTE_DEFINE_PER_LCORE(unsigned long, last_sched_time);
RTE_DEFINE_PER_LCORE(unsigned long, last_sched_time_be);
RTE_DEFINE_PER_LCORE(unsigned long, local_extra_demand);
RTE_DEFINE_PER_LCORE(unsigned long, local_leftover_tokens);
RTE_DEFINE_PER_LCORE(int, roundrobin_start);

static int nvme_compute_req_cost(int req_type, size_t req_len);

static void set_token_deficit_limit(void);

struct nvme_ctx * alloc_local_nvme_ctx(void)
{
	return mempool_alloc(&percpu_get(ctx_mempool));
}

extern void free_local_nvme_ctx(struct nvme_ctx *req)
{
	mempool_free(&percpu_get(ctx_mempool),req);
}

struct nvme_sw_queue * alloc_local_nvme_swq(void)
{
	return  mempool_alloc(&percpu_get(nvme_swq_mempool));
}

void free_local_nvme_swq(struct nvme_sw_queue *q)
{
	mempool_free(&percpu_get(nvme_swq_mempool),q);
}
/**
 * init_nvme_request_cpu - allocates the core-local nvme request region
 *
 * Returns 0 if successful, otherwise failure.
 */
int init_nvme_request_cpu(void)
{
	struct nvme_tenant_mgmt* thread_tenant_manager;
	int ret;

	if (percpu_get(mempool_initialized)) {
		return 0;
	}

	if (CFG.num_nvmedev == 0) {
		printf("No NVMe devices found, skipping initialization\n");
		return 0;
	}
		
	struct mempool *m2 = &percpu_get(ctx_mempool);
	ret = mempool_create(m2, &ctx_datastore, MEMPOOL_SANITY_PERCPU, percpu_get(cpu_id));
	if(ret) {
	  //FIXME: implement mempool destroy
		//mempool_destroy(m);
		return ret;
	}

	// initialize sw queue
	
	struct mempool *m3 = &percpu_get(nvme_swq_mempool);
	ret = mempool_create(m3, &nvme_swq_datastore, MEMPOOL_SANITY_PERCPU,percpu_get(cpu_id));
	if(ret) {
	  //FIXME: implement mempool destroy
		//mempool_destroy(m);
		return ret;
	}
	
	thread_tenant_manager = &percpu_get(nvme_tenant_manager);
	list_head_init(&thread_tenant_manager->tenant_swq);
	thread_tenant_manager->num_tenants = 0;
	thread_tenant_manager->num_best_effort_tenants = 0;

	percpu_get(last_sched_time) = timer_now();
	percpu_get(last_sched_time_be) = rdtsc(); //timer_now();
	percpu_get(local_leftover_tokens) = 0;
	percpu_get(local_extra_demand) = 0;
	percpu_get(mempool_initialized) = true;

	//LIBAIO
	io_setup(MAX_EVENTS, &thread_tenant_manager->aio_ctx);
	thread_tenant_manager->events = calloc(MAX_EVENTS, sizeof(struct io_event));
	if (!thread_tenant_manager->events){
		printf("ERROR: could not allocate events array for libaio\n");
		return -1;
	}

	return ret;
}

/**
 * init_nvme_request- allocate global nvme request mempool
 */
int init_nvme_request(void)
{
	int ret;
	struct mempool_datastore *m = &request_datastore;
	struct mempool_datastore *m2 = &ctx_datastore;
	struct mempool_datastore *m3 = &nvme_swq_datastore;

	if (CFG.num_nvmedev == 0) {
		return 0;
	}

	ret = mempool_create_datastore(m2, NUM_NVME_REQUESTS, sizeof(struct nvme_ctx), "nvme_ctx");
	if (ret) {
		//mempool_pagemem_destroy(m);
		return ret;
	}

	/*
	ret = mempool_pagemem_map_to_user(m2);
	if (ret) {
		mempool_pagemem_destroy(m);
		mempool_pagemem_destroy(m2);
		return ret;
	}
	*/

	// memory for software queues for nvme scheduling
	ret = mempool_create_datastore(m3, (MEMPOOL_DEFAULT_CHUNKSIZE*MAX_NVME_FLOW_GROUPS)/MEMPOOL_DEFAULT_CHUNKSIZE * 2, 
								   sizeof(struct nvme_sw_queue), "nvme_swq");
	if (ret) {
		//mempool_pagemem_destroy(m);
		return ret;
	}

	//need to alloc req mempool for admin queue
	init_nvme_request_cpu();

	set_token_deficit_limit();
	
	return 0;
}

/**
 * nvme_request_exit_cpu - frees the core-local nvme request region
 */
void nvme_request_exit_cpu(void)
{
	//mempool_pagemem_destroy(&request_datastore);
	//mempool_pagemem_destroy(&ctx_datastore);
	//mempool_pagemem_destroy(&nvme_swq_datastore);
}



static bool
probe_cb(void *cb_ctx, struct spdk_pci_device *dev, struct spdk_nvme_ctrlr_opts *opts)
{
	printf("probe return\n"); 
	if (dev == NULL) {
		log_err("nvmedev: failed to start driver\n");
		return -ENODEV;
	}

	printf("attaching to nvme device\n");
	return true;
}

static void
attach_cb(void *cb_ctx, struct spdk_pci_device *dev, struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts)
{
	unsigned int num_ns, nsid;
	const struct spdk_nvme_ctrlr_data *cdata;
	struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, 1);
	
	bitmap_init(ioq_bitmap, MAX_NUM_IO_QUEUES, 0);
	nvme_ctrlr = ctrlr;
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		printf("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
		       cdata->mn, cdata->sn,
		       spdk_nvme_ns_get_id(ns));
		return;
	}

	printf("Attached to device %-20.20s (%-20.20s) controller: %p\n", cdata->mn, cdata->sn, ctrlr);

	num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	printf("Found %i namespaces\n", num_ns);
	for (nsid = 1; nsid <= num_ns; nsid++) {
		struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		printf("NS: %i, size: %lx\n", nsid, spdk_nvme_ns_get_size(ns));
	}
}



/**
 * nvmedev_init - initializes nvme devices
 *
 * Returns 0 if successful, otherwise fail.
 */
int init_nvmedev(void)
{

	if (CFG.num_nvmedev > 1)
		printf("IX suupports only one NVME device, ignoring all further devices\n");
	if (CFG.num_nvmedev == 0)
		return 0;
	
	printf("device path is: %s\n", CFG.nvmedev[0]);
	fd = open(CFG.nvmedev[0], O_RDWR | O_DIRECT);
	if (fd < 0){
		printf("ERROR: could not open device: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

int init_nvmeqp_cpu(void)
{
	if (CFG.num_nvmedev == 0)
		return 0;

	return 0; // for libaio don't need this function, so do nothing!!	
}

void nvmedev_exit(void)
{
	struct spdk_nvme_ctrlr *nvme = nvme_ctrlr;
	if (!nvme)
		return;
}

int allocate_nvme_ioq(void)
{
	int q;

	spin_lock(&nvme_bitmap_lock);	
	for (q = 1; q < MAX_NUM_IO_QUEUES; q++) {
		if (bitmap_test(ioq_bitmap, q))
			continue;
		bitmap_set(ioq_bitmap, q);
		break;
	}
	spin_unlock(&nvme_bitmap_lock);	
    
	if (q == MAX_NUM_IO_QUEUES) {
		return -ENOMEM;
	}
	
	return q;
}

struct nvme_string {
	uint16_t	value;
	const char 	*str;
};
static const struct nvme_string generic_status[] = {
	{ SPDK_NVME_SC_SUCCESS, "SUCCESS" },
	{ SPDK_NVME_SC_INVALID_OPCODE, "INVALID OPCODE" },
	{ SPDK_NVME_SC_INVALID_FIELD, "INVALID_FIELD" },
	{ SPDK_NVME_SC_COMMAND_ID_CONFLICT, "COMMAND ID CONFLICT" },
	{ SPDK_NVME_SC_DATA_TRANSFER_ERROR, "DATA TRANSFER ERROR" },
	{ SPDK_NVME_SC_ABORTED_POWER_LOSS, "ABORTED - POWER LOSS" },
	{ SPDK_NVME_SC_INTERNAL_DEVICE_ERROR, "INTERNAL DEVICE ERROR" },
	{ SPDK_NVME_SC_ABORTED_BY_REQUEST, "ABORTED - BY REQUEST" },
	{ SPDK_NVME_SC_ABORTED_SQ_DELETION, "ABORTED - SQ DELETION" },
	{ SPDK_NVME_SC_ABORTED_FAILED_FUSED, "ABORTED - FAILED FUSED" },
	{ SPDK_NVME_SC_ABORTED_MISSING_FUSED, "ABORTED - MISSING FUSED" },
	{ SPDK_NVME_SC_INVALID_NAMESPACE_OR_FORMAT, "INVALID NAMESPACE OR FORMAT" },
	{ SPDK_NVME_SC_COMMAND_SEQUENCE_ERROR, "COMMAND SEQUENCE ERROR" },
	{ SPDK_NVME_SC_LBA_OUT_OF_RANGE, "LBA OUT OF RANGE" },
	{ SPDK_NVME_SC_CAPACITY_EXCEEDED, "CAPACITY EXCEEDED" },
	{ SPDK_NVME_SC_NAMESPACE_NOT_READY, "NAMESPACE NOT READY" },
	{ 0xFFFF, "GENERIC" }
};

static const struct nvme_string command_specific_status[] = {
	{ SPDK_NVME_SC_COMPLETION_QUEUE_INVALID, "INVALID COMPLETION QUEUE" },
	{ SPDK_NVME_SC_INVALID_QUEUE_IDENTIFIER, "INVALID QUEUE IDENTIFIER" },
	{ SPDK_NVME_SC_MAXIMUM_QUEUE_SIZE_EXCEEDED, "MAX QUEUE SIZE EXCEEDED" },
	{ SPDK_NVME_SC_ABORT_COMMAND_LIMIT_EXCEEDED, "ABORT CMD LIMIT EXCEEDED" },
	{ SPDK_NVME_SC_ASYNC_EVENT_REQUEST_LIMIT_EXCEEDED, "ASYNC LIMIT EXCEEDED" },
	{ SPDK_NVME_SC_INVALID_FIRMWARE_SLOT, "INVALID FIRMWARE SLOT" },
	{ SPDK_NVME_SC_INVALID_FIRMWARE_IMAGE, "INVALID FIRMWARE IMAGE" },
	{ SPDK_NVME_SC_INVALID_INTERRUPT_VECTOR, "INVALID INTERRUPT VECTOR" },
	{ SPDK_NVME_SC_INVALID_LOG_PAGE, "INVALID LOG PAGE" },
	{ SPDK_NVME_SC_INVALID_FORMAT, "INVALID FORMAT" },
	{ SPDK_NVME_SC_CONFLICTING_ATTRIBUTES, "CONFLICTING ATTRIBUTES" },
	{ SPDK_NVME_SC_INVALID_PROTECTION_INFO, "INVALID PROTECTION INFO" },
	{ SPDK_NVME_SC_ATTEMPTED_WRITE_TO_RO_PAGE, "WRITE TO RO PAGE" },
	{ 0xFFFF, "COMMAND SPECIFIC" }
};

static const struct nvme_string media_error_status[] = {
	{ SPDK_NVME_SC_WRITE_FAULTS, "WRITE FAULTS" },
	{ SPDK_NVME_SC_UNRECOVERED_READ_ERROR, "UNRECOVERED READ ERROR" },
	{ SPDK_NVME_SC_GUARD_CHECK_ERROR, "GUARD CHECK ERROR" },
	{ SPDK_NVME_SC_APPLICATION_TAG_CHECK_ERROR, "APPLICATION TAG CHECK ERROR" },
	{ SPDK_NVME_SC_REFERENCE_TAG_CHECK_ERROR, "REFERENCE TAG CHECK ERROR" },
	{ SPDK_NVME_SC_COMPARE_FAILURE, "COMPARE FAILURE" },
	{ SPDK_NVME_SC_ACCESS_DENIED, "ACCESS DENIED" },
	{ 0xFFFF, "MEDIA ERROR" }
};


static const char *
nvme_get_string(const struct nvme_string *strings, uint16_t value)
{
	const struct nvme_string *entry;

	entry = strings;

	while (entry->value != 0xFFFF) {
		if (entry->value == value) {
			return entry->str;
		}
		entry++;
	}
	return entry->str;
}

static const char *
get_status_string(uint16_t sct, uint16_t sc)
{
	const struct nvme_string *entry;

	switch (sct) {
	case SPDK_NVME_SCT_GENERIC:
		entry = generic_status;
		break;
	case SPDK_NVME_SCT_COMMAND_SPECIFIC:
		entry = command_specific_status;
		break;
	case SPDK_NVME_SCT_MEDIA_ERROR:
		entry = media_error_status;
		break;
	case SPDK_NVME_SCT_VENDOR_SPECIFIC:
		return "VENDOR SPECIFIC";
	default:
		return "RESERVED";
	}

	return nvme_get_string(entry, sc);
}



void
nvme_write_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct nvme_ctx *n_ctx = (struct nvme_ctx *) ctx;

	if (spdk_nvme_cpl_is_error(cpl)){
		printf("SPDK Write Failed!\n");
		printf("%s (%02x/%02x) sqid:%d cid:%d cdw0:%x sqhd:%04x p:%x m:%x dnr:%x\n",
		       get_status_string(cpl->status.sct, cpl->status.sc),
		       cpl->status.sct, cpl->status.sc, cpl->sqid, cpl->cid, cpl->cdw0,
		       cpl->sqhd, cpl->status.p, cpl->status.m, cpl->status.dnr);
	}

	usys_nvme_written(n_ctx->cookie, RET_OK);
	
	free_local_nvme_ctx(n_ctx);
}

void
nvme_read_cb(void *ctx, const struct spdk_nvme_cpl *cpl)
{
	struct nvme_ctx *n_ctx = (struct nvme_ctx *) ctx;

	printf("nvme_read_cb!\n");
	if (spdk_nvme_cpl_is_error(cpl)){
		printf("SPDK Read Failed!\n");
		printf("%s (%02x/%02x) sqid:%d cid:%d cdw0:%x sqhd:%04x p:%x m:%x dnr:%x\n",
		       get_status_string(cpl->status.sct, cpl->status.sc),
		       cpl->status.sct, cpl->status.sc, cpl->sqid, cpl->cid, cpl->cdw0,
		       cpl->sqhd, cpl->status.p, cpl->status.m, cpl->status.dnr);
	}
	
	usys_nvme_response(n_ctx->cookie, n_ctx->user_buf.buf, RET_OK);
	
	free_local_nvme_ctx(n_ctx);
}

long bsys_nvme_open(long dev_id, long ns_id)
{
	struct spdk_nvme_ns *ns;
	int ioq;
	
	// FIXME: for now, only support 1 namespace
	if (ns_id != global_ns_id) {
		panic("ERROR: only support 1 namespace with ns_id = 1, ns_id: %lx\n", ns_id);
		return -RET_INVAL;
	}
	// allocate next available queue 
	// for now, assume only one bitmap
	// FIXME: we may want 1 bitmap per device 
	ioq = allocate_nvme_ioq();
	if (ioq < 0){
		return -RET_NOBUFS; 
	}
	bitmap_init(nvme_fgs_bitmap, MAX_NVME_FLOW_GROUPS, 0);

	percpu_get(open_ev[percpu_get(open_ev_ptr)++]) = ioq;
	if (spdk_fd_get_size(fd) > 0){ //NOTE this only works for nvme devices...
		global_ns_size = spdk_fd_get_size(fd);
	}
	global_ns_sector_size = spdk_fd_get_blocklen(fd);
	printf("global_ns size is %lu, sector size is %lu\n", global_ns_size, global_ns_sector_size);
	return RET_OK;
}

long bsys_nvme_close(long dev_id, long ns_id, hqu_t handle)
{
	printf("BSYS NVME CLOSE\n");
	// FIXME: for now, only support 1 namespace
	if (ns_id != global_ns_id) {
		usys_nvme_closed(-RET_INVAL, -RET_INVAL);
		panic("ERROR: only support 1 namespace with ns_id = 1\n");
		return -RET_INVAL;
	}
	bitmap_clear(ioq_bitmap, handle);
	usys_nvme_closed(handle, 0);
	return RET_OK;
}

int set_nvme_flow_group_id(long flow_group_id, long* fg_handle_to_set)
{
	int i;
    int next_avail = 0;
	// first check if already registered this flow	
	spin_lock(&nvme_bitmap_lock);	
	for (i = 1; i < MAX_NVME_FLOW_GROUPS; i++) {
		if (bitmap_test(nvme_fgs_bitmap, i)){
			// if already registered this flow group, return its index
			if (nvme_fgs[i].flow_group_id == flow_group_id &&
				nvme_fgs[i].tid == RTE_PER_LCORE(cpu_nr)){
				*fg_handle_to_set = i;
    			spin_unlock(&nvme_bitmap_lock);	
				return 1;
			}
		}
		else {
			if (next_avail == 0){
				next_avail = i;
			}	
		}
	}

	if (next_avail == MAX_NVME_FLOW_GROUPS) {
    	spin_unlock(&nvme_bitmap_lock);	
		return -ENOMEM;
	}
	
	bitmap_set(nvme_fgs_bitmap, next_avail);
    spin_unlock(&nvme_bitmap_lock);	
	
	*fg_handle_to_set = next_avail;
	return 0;
}

// adjust token deficit limit to allow LC tenants to burst, but not too much
static void set_token_deficit_limit(void){
	printf("DEVICE PARAMS: read cost %d, write cost %d\n", NVME_READ_COST, NVME_WRITE_COST);
	TOKEN_DEFICIT_LIMIT = 100*NVME_WRITE_COST; 
}



static unsigned long find_token_limit_from_devmodel(unsigned int lat_SLO){
	int i=0;
	unsigned long y0, y1, x0, x1;
	double y;

	for (i=0; i < dev_model_size; i++){
		if (lat_SLO < dev_model[i].p95_tail_latency){
			break;
		}	
	}	
	if (i > 0){
		if (global_readonly_flag){
			if (i == dev_model_size){
				return dev_model[i-1].token_rdonly_rate_limit;
			}
			// linear interpolation of token limits provided in devmodel config file
			y0 = dev_model[i-1].token_rdonly_rate_limit;
			y1 = dev_model[i].token_rdonly_rate_limit; 
			x0 = dev_model[i-1].p95_tail_latency;
			x1 = dev_model[i].p95_tail_latency;
			assert(x1-x0 != 0);
			y = y0 + ((y1 - y0) * (lat_SLO - x0) / (double) (x1 - x0));
			return  (unsigned long) y;

		}
		else {
			if (i == dev_model_size){
				return dev_model[i-1].token_rate_limit;
			}
			// linear interpolation of token limits provided in devmodel config file
			y0 = dev_model[i-1].token_rate_limit;
			y1 = dev_model[i].token_rate_limit; 
			x0 = dev_model[i-1].p95_tail_latency;
			x1 = dev_model[i].p95_tail_latency;
			y = y0 + ((y1 - y0) * (lat_SLO - x0) / (double) (x1 - x0));
			assert(x1-x0 != 0);
			return  (unsigned long) y;
		}
	}
	printf("WARNING: provide dev model info for latency SLO %d\n", lat_SLO);	
	if (global_readonly_flag){
		return dev_model[0].token_rdonly_rate_limit;
	}
	return dev_model[0].token_rate_limit; 

}


unsigned long lookup_device_token_rate(unsigned int lat_SLO){

	switch (nvme_dev_model) {
		case DEFAULT_FLASH:
			return UINT_MAX;
		case FAKE_FLASH:
			return UINT_MAX;
		case FLASH_DEV_MODEL:
			return find_token_limit_from_devmodel(lat_SLO);
		default:
			printf("WARNING: undefined flash device model\n");
			return UINT_MAX;

	}

	log_err("Set device lookup for lat SLO %ld!\n", lat_SLO);	
	return 500000;
}

unsigned long scaled_IOPS(unsigned long IOPS, int rw_ratio_100){
	double scaledIOPS;
	double rw_ratio = (double) rw_ratio_100 / (double) 100;

	/*
	 * NOTE: when calculating token reservation for latency-critical tenants,
	 * 		 assume SLO specificed for 4kB requests
	 * 		 e.g. if your application's IOPS SLO is 100K IOPS for 8K IOs, 
	 * 		      register your app's SLO with ReFlex as 200K IOPS 
	 */
	scaledIOPS = (IOPS * rw_ratio * nvme_compute_req_cost(NVME_CMD_READ, SLO_REQ_SIZE)) 
					+ (IOPS * (1-rw_ratio) * nvme_compute_req_cost(NVME_CMD_WRITE, SLO_REQ_SIZE));
	return (unsigned long) (scaledIOPS + 0.5);
}

static void readjust_lc_tenant_token_limits(void){
	int i,j = 0;
	for (i = 0; i < MAX_NVME_FLOW_GROUPS; i++){
		if (bitmap_test(nvme_fgs_bitmap, i)) {
			if (nvme_fgs[i].latency_critical_flag) {
				nvme_fgs[i].scaled_IOPuS_limit = (nvme_fgs[i].scaled_IOPS_limit + global_lc_boost_no_BE) / (double) 1E6;
				j++;
				if (j == global_num_lc_tenants){
					return;
				}
			}
		}
	}

}


int recalculate_weights_add(long new_flow_group_idx){
	unsigned long new_global_token_rate = 0;
	unsigned long new_global_LC_sum_token_rate = 0;
	unsigned long lc_token_rate_boost_when_no_BE = 0;
	unsigned int be_token_rate_per_tenant;


	spin_lock(&nvme_bitmap_lock);	
	
	if (nvme_fgs[new_flow_group_idx].latency_critical_flag) {
		new_global_LC_sum_token_rate = global_LC_sum_token_rate + nvme_fgs[new_flow_group_idx].scaled_IOPS_limit;
		if (nvme_fgs[new_flow_group_idx].rw_ratio_SLO < 100){
			global_readonly_flag = false;
		}
		
		new_global_token_rate = lookup_device_token_rate(nvme_fgs[new_flow_group_idx].latency_us_SLO);
		if (new_global_token_rate > global_token_rate){
			new_global_token_rate = global_token_rate; // keep limit based on strictest latency SLO
		}
		
		if (new_global_LC_sum_token_rate > new_global_token_rate){
			// control plane notifies tenant can't meet its SLO
			// don't update the global token rate since won't regsiter this tenant
			log_err("CANNOT SATISFY TENANT's SLO: %lu > %lu\n", new_global_LC_sum_token_rate, new_global_token_rate);
			spin_unlock(&nvme_bitmap_lock);	
			return -RET_CANTMEETSLO;
		}
	
		global_token_rate = new_global_token_rate;
		global_LC_sum_token_rate = new_global_LC_sum_token_rate;
		printf("Global token rate: %lu tokens/s.\n", global_token_rate);
		global_num_lc_tenants++;
	}
	else{
		global_num_best_effort_tenants++;
		global_readonly_flag = false; // assume BE tenant has rd/wr mixed workload
	}	
	
	if (global_num_best_effort_tenants){
	   	be_token_rate_per_tenant = (global_token_rate - global_LC_sum_token_rate) / global_num_best_effort_tenants;
		lc_token_rate_boost_when_no_BE = 0;
	}
	else{
		be_token_rate_per_tenant = 0;
		if (global_num_lc_tenants)
			lc_token_rate_boost_when_no_BE = (global_token_rate - global_LC_sum_token_rate) / global_num_lc_tenants;
	}
	atomic_write(&global_be_token_rate_per_tenant, be_token_rate_per_tenant);
	
	// if number of BE tenants has changes from 0 to 1 or more (or vice versa)
	// adjust LC tenant boost (only want to boost if no BE tenants registered)
	if (lc_token_rate_boost_when_no_BE != global_lc_boost_no_BE){
		global_lc_boost_no_BE = lc_token_rate_boost_when_no_BE;
		readjust_lc_tenant_token_limits();
	}
	spin_unlock(&nvme_bitmap_lock);	
	
	return 1;
}

int recalculate_weights_remove(long flow_group_idx){
	long i;
	unsigned int strictest_latency_SLO = UINT_MAX;
	unsigned int be_token_rate_per_tenant;
	unsigned long lc_token_rate_boost_when_no_BE = 0;


	spin_lock(&nvme_bitmap_lock);	

	if (nvme_fgs[flow_group_idx].latency_critical_flag) {
		//find new strictest latency SLO
		global_readonly_flag = true;
		for (i = 0; i < MAX_NVME_FLOW_GROUPS; i++){
			if (bitmap_test(nvme_fgs_bitmap, i) && i != flow_group_idx) {
				if (nvme_fgs[i].latency_critical_flag) {
					if(nvme_fgs[i].latency_us_SLO < strictest_latency_SLO){
						strictest_latency_SLO = nvme_fgs[i].latency_us_SLO;
					}
					if(nvme_fgs[i].rw_ratio_SLO < 100){
						global_readonly_flag = false;
					}
				}
			}
		}
		global_LC_sum_token_rate -= nvme_fgs[flow_group_idx].scaled_IOPS_limit;
		global_token_rate = lookup_device_token_rate(strictest_latency_SLO);
		
		printf("Global token rate: %lu tokens/s\n", global_token_rate);

		global_num_lc_tenants--;
	}
	else{
		global_num_best_effort_tenants--;
	}	
	
	if (global_num_best_effort_tenants){
		global_readonly_flag = false;
	   	be_token_rate_per_tenant = (global_token_rate - global_LC_sum_token_rate) / global_num_best_effort_tenants;
		lc_token_rate_boost_when_no_BE = 0;
	}
	else{
		be_token_rate_per_tenant = 0;
		if (global_num_lc_tenants)
			lc_token_rate_boost_when_no_BE = (global_token_rate - global_LC_sum_token_rate) / global_num_lc_tenants;
	}
	atomic_write(&global_be_token_rate_per_tenant, be_token_rate_per_tenant);

	// if number of BE tenants has changes from 0 to 1 or more (or vice versa)
	// adjust LC tenant boost (only want to boost if no BE tenants registered)
	if (lc_token_rate_boost_when_no_BE != global_lc_boost_no_BE){
		global_lc_boost_no_BE = lc_token_rate_boost_when_no_BE;
		readjust_lc_tenant_token_limits();
	}

	spin_unlock(&nvme_bitmap_lock);	

	return 1;
}


//TODO: consider implementing separate per-thread lists for BE and LC tenants (will simplify some code for scheduler)
long bsys_nvme_register_flow(long flow_group_id, unsigned long cookie, 
							 unsigned int latency_us_SLO, unsigned long IOPS_SLO, 
							 int rw_ratio_SLO  )
{
	long fg_handle = 0;
	struct nvme_flow_group* nvme_fg;
	int ret = 0;
	int already_registered_flow = 0;
	struct nvme_tenant_mgmt* thread_tenant_manager;
	struct nvme_sw_queue* swq;

	already_registered_flow = set_nvme_flow_group_id(flow_group_id, &fg_handle);
   	if (fg_handle < 0 ){
		log_err("error: exceeded max (%d) nvme flow groups!\n", MAX_NVME_FLOW_GROUPS);
	}

	nvme_fg = &nvme_fgs[fg_handle];
	
	nvme_fg->flow_group_id = flow_group_id;
	nvme_fg->cookie = cookie;
	nvme_fg->latency_us_SLO = latency_us_SLO;
	nvme_fg->IOPS_SLO = IOPS_SLO;
	nvme_fg->rw_ratio_SLO = rw_ratio_SLO;
	nvme_fg->scaled_IOPS_limit = scaled_IOPS(IOPS_SLO, rw_ratio_SLO);
	nvme_fg->tid = RTE_PER_LCORE(cpu_nr);

	if (already_registered_flow == 1 
		&& nvme_fg->scaled_IOPS_limit != scaled_IOPS(IOPS_SLO, rw_ratio_SLO)){
		/* 
		 * A tenant is a logical grouping for an app's connections that want the *same* SLO
		 * so if a tenant is trying to register different SLOs across connections, give warning
		 * should register these connections as separate tenants 
		 *
		 * Default way to procede here is to overwrite the whole tenant's SLO with the new one 
		 *
		 */
		printf("warning: tenant connection registered different SLO, will overwrite previous SLO for all of this tenant's connections. 1 SLO per tenant.\n");
		nvme_fg->scaled_IOPuS_limit = nvme_fg->scaled_IOPS_limit / (double) 1E6; 
	}
	
	if (latency_us_SLO == 0){
		nvme_fg->latency_critical_flag = false;
	}
	else{ 
		nvme_fg->latency_critical_flag = true;
	}

	if (already_registered_flow == 0){
		nvme_fg->scaled_IOPuS_limit = nvme_fg->scaled_IOPS_limit / (double) 1E6; 
		ret = recalculate_weights_add(fg_handle); 
		if (ret < 0) {
			printf("warning: cannot satisfy SLO\n"); 
			return -RET_CANTMEETSLO;
		}

		swq = alloc_local_nvme_swq();
		if (swq == NULL) {
			log_err("error: can't allocate nvme_swq for flow group\n");
			return -RET_NOMEM;
		}	
		nvme_fg->nvme_swq = swq;
		nvme_sw_queue_init(swq, fg_handle);
		thread_tenant_manager = &percpu_get(nvme_tenant_manager);
		list_add(&thread_tenant_manager->tenant_swq, &swq->list);
		thread_tenant_manager->num_tenants++;
		percpu_get(roundrobin_start) = 0; 
		nvme_fg->conn_ref_count = 0;
		if (latency_us_SLO == 0){
			thread_tenant_manager->num_best_effort_tenants++;
		}
		
		if (latency_us_SLO == 0){
			printf("Register tenant %ld (port id: %ld). Managed by thread %ld. Best-effort tenant. \n", 
					 fg_handle, flow_group_id, RTE_PER_LCORE(cpu_nr));
		}
		else{
			printf("Register tenant %ld (port id: %ld). Managed by thread %ld. IOPS_SLO: %lu, r/w %d, scaled_IOPS: %lu tokens/s, latency SLO: %lu us. \n", 
					 fg_handle, flow_group_id, RTE_PER_LCORE(cpu_nr),  IOPS_SLO, rw_ratio_SLO, nvme_fg->scaled_IOPS_limit, latency_us_SLO);
		}
	}
	nvme_fg->conn_ref_count++;
	
	usys_nvme_registered_flow(fg_handle, cookie, RET_OK);

	return RET_OK;
}

long bsys_nvme_unregister_flow(long fg_handle) 
{
	struct nvme_tenant_mgmt* thread_tenant_manager;
	
	nvme_fgs[fg_handle].conn_ref_count--;
	if (nvme_fgs[fg_handle].conn_ref_count == 0){
		thread_tenant_manager = &percpu_get(nvme_tenant_manager);
		if (!nvme_fgs[fg_handle].latency_critical_flag){
			thread_tenant_manager->num_best_effort_tenants--;
		}
		list_del(&nvme_fgs[fg_handle].nvme_swq->list);
		free_local_nvme_swq(nvme_fgs[fg_handle].nvme_swq);	
		thread_tenant_manager->num_tenants--;
		recalculate_weights_remove(fg_handle);

		spin_lock(&nvme_bitmap_lock);	
		bitmap_clear(nvme_fgs_bitmap, fg_handle);
		spin_unlock(&nvme_bitmap_lock);
	}
	
	usys_nvme_unregistered_flow(fg_handle, RET_OK);
	
	return RET_OK;
}

// request cost scales linearly with size above 4KB
// note: may need to adjust this if does not match your Flash device behavior
static int nvme_compute_req_cost(int req_type, size_t req_len) 
{
	if (req_len <= 0){
		printf("ERROR: request size <= 0!\n");
		return 0;
	}

	int len_scale_factor = 1;

	if (req_len > 4096){
		// divide req_len by 4096 and round up
		len_scale_factor = (req_len  + 4096 -1 )/ 4096;
	}

	if (req_type == NVME_CMD_READ){
		return NVME_READ_COST * len_scale_factor;
	}
	else if (req_type == NVME_CMD_WRITE) {
		return NVME_WRITE_COST * len_scale_factor;
	}
	return 1;
}

void set_iocb(struct iocb* iocb, int cmd, void* paddr, unsigned long lba, 
			  unsigned int lba_count, unsigned long ctx_addr) {

	if (cmd == NVME_CMD_READ)
		iocb->aio_lio_opcode = IO_CMD_PREAD;
	else if (cmd == NVME_CMD_WRITE)
		iocb->aio_lio_opcode = IO_CMD_PWRITE;
	else 
		printf("ERROR: unknown cmd\n");

	iocb->aio_fildes = fd;
	iocb->u.c.buf = paddr;
	iocb->u.c.nbytes = lba_count * global_ns_sector_size;
	iocb->u.c.offset = lba * global_ns_sector_size; //FIXME: check this
	iocb->data = ctx_addr;
}

long bsys_nvme_write(hqu_t fg_handle, void __user *__restrict vaddr, unsigned long lba,
		     unsigned int lba_count, unsigned long cookie)
{
	struct spdk_nvme_ns *ns;
	struct nvme_ctx *ctx;
	void* paddr;
	int ret;
	struct nvme_tenant_mgmt* manager;
	struct iocb* iocb = &ctx->iocb;
	manager = &percpu_get(nvme_tenant_manager);

	ctx = alloc_local_nvme_ctx();
	if (ctx == NULL) {
		printf("ERROR: Cannot allocate memory for nvme_ctx in bsys_nvme_write\n");
		return -RET_NOMEM;
	}
	ctx->cookie = cookie;
	
	paddr = vaddr;	

	if (nvme_sched_flag){
		// Store all info in ctx before add to software queue
		ctx->tid = RTE_PER_LCORE(cpu_nr);
		ctx->fg_handle = fg_handle; 
		ctx->cmd = NVME_CMD_WRITE;
		ctx->req_cost = nvme_compute_req_cost(NVME_CMD_WRITE, lba_count * global_ns_sector_size);
		ctx->paddr = paddr;
		ctx->lba = lba;
		ctx->lba_count = lba_count;

		set_iocb(&ctx->iocb, NVME_CMD_WRITE, paddr, lba, lba_count, ctx);

		// add to SW queue
		//struct nvme_sw_queue swq = percpu_get(nvme_swq[ctx->priority]);
		struct nvme_sw_queue* swq = nvme_fgs[fg_handle].nvme_swq;
		ret = nvme_sw_queue_push_back(swq, ctx);
		if (ret != 0) {
			free_local_nvme_ctx(ctx);
			return -RET_NOMEM;
		}
	}
	else {
		set_iocb(&ctx->iocb, NVME_CMD_WRITE, paddr, lba, lba_count, ctx);
		//ret = spdk_nvme_ns_cmd_write(ns, percpu_get(qpair), paddr, lba, lba_count, nvme_write_cb, ctx, 0);
		ret = io_submit(manager->aio_ctx, 1, &iocb);
		if(ret != 0)
			printf("NVME Write ret: %lx\n", ret);
		assert(ret == 0);
	}

	return RET_OK;
}



long bsys_nvme_read(hqu_t fg_handle, void __user *__restrict vaddr, unsigned long lba,
		    unsigned int lba_count, unsigned long cookie)
{
	struct spdk_nvme_ns *ns;
	struct nvme_ctx *ctx;
	void* paddr;
	unsigned int ns_sector_size;
	int ret;
	struct nvme_tenant_mgmt* manager;
	struct iocb* iocb = &ctx->iocb;
	manager = &percpu_get(nvme_tenant_manager);
	
	ctx = alloc_local_nvme_ctx();
	if (ctx == NULL) {
		printf("ERROR: Cannot allocate memory for nvme_ctx in bsys_nvme_read\n");
		return -RET_NOMEM;
	}
	ctx->cookie = cookie;
	
	paddr = vaddr;
		
	ctx->user_buf.buf = vaddr;
	
	if (nvme_sched_flag) {
		// Store all info in ctx before add to software queue
		ctx->tid = RTE_PER_LCORE(cpu_nr);
		ctx->fg_handle = fg_handle; 
		ctx->cmd = NVME_CMD_READ;
		ctx->req_cost = nvme_compute_req_cost(NVME_CMD_READ, lba_count * global_ns_sector_size);
		ctx->paddr = paddr;
		ctx->lba = lba;
		ctx->lba_count = lba_count;

		set_iocb(&ctx->iocb, NVME_CMD_READ, paddr, lba, lba_count, ctx);

		// add to SW queue
		struct nvme_sw_queue* swq = nvme_fgs[fg_handle].nvme_swq;
		ret = nvme_sw_queue_push_back(swq, ctx);
		if (ret != 0) {
			free_local_nvme_ctx(ctx);
			return -RET_NOMEM;
		}
	}
	else {
		set_iocb(&ctx->iocb, NVME_CMD_READ, paddr, lba, lba_count, ctx);
		assert(((lba / lba_count) * lba_count) == lba);
		ret = io_submit(manager->aio_ctx, 1, &iocb);
		//ret = spdk_nvme_ns_cmd_read(ns, percpu_get(qpair), paddr, lba, lba_count, nvme_read_cb, ctx, 0);
		if(ret != 0)
			printf("NVME Read ret: %lx\n", ret);
		assert(ret == 0);
	}

	return RET_OK;
}

static void sgl_reset_cb(void *cb_arg, uint32_t sgl_offset)
{
	struct nvme_ctx *ctx = (struct nvme_ctx *)cb_arg;

	ctx->user_buf.sgl_buf.current_sgl = sgl_offset / SGL_PAGE_SIZE;
}

static int sgl_next_cb(void *cb_arg, uint64_t *address, uint32_t *length)
{
	void *paddr;
	void __user *__restrict temp;
	struct nvme_ctx *ctx = (struct nvme_ctx *)cb_arg;

	if (ctx->user_buf.sgl_buf.current_sgl >= ctx->user_buf.sgl_buf.num_sgls) {
		*address = 0;
		*length = 0;
		printf("Warning: nvme req size mismatch\n"); 
		assert(0);
	}
	else {
		temp = ctx->user_buf.sgl_buf.sgl[ctx->user_buf.sgl_buf.current_sgl];
		ctx->user_buf.sgl_buf.current_sgl++;
		/*
		paddr = (void *) vm_lookup_phys(temp, PGSIZE_2MB);
		if (unlikely(!paddr)) {
			printf("bsys_nvme_read: no paddr for requested buf!");
			return -RET_FAULT;
		}
		*/
		//virt to phys
		//*address = (uint64_t) ((uintptr_t) paddr + PGOFF_2MB(temp));
		//*address = nvme_vtophys((void *)*address);
		//temp = rte_malloc_virt2phy(temp); //FIXME: use this for rte_malloc req buf only
		*address = (void *)temp;
		//*address = rte_mem_virt2phy((void *)temp);
		//printf("translated %p address is %x, current sgl is %d, num_sgls is %d, handle %lu, tid %u\n", 
		//	   temp, *address, ctx->user_buf.sgl_buf.current_sgl -1, ctx->user_buf.sgl_buf.num_sgls, ctx->handle, ctx->tid);	
		*length = 4096; //PGSIZE_4KB
	}
	return 0;
}
//TODO: use writev in reflex_server --> IO_CMD_PWRITEV aio command
long bsys_nvme_writev(hqu_t fg_handle, void __user **__restrict buf, int num_sgls,
		     unsigned long lba, unsigned int lba_count, unsigned long cookie)
{
	struct spdk_nvme_ns *ns;
	struct nvme_ctx *ctx;
	int ret;
	
	ctx = alloc_local_nvme_ctx();
	if (ctx == NULL) {
		printf("ERROR: Cannot allocate memory for nvme_ctx in bsys_nvme_read\n");
		return -RET_NOMEM;
	}
	ctx->cookie = cookie;
	ctx->user_buf.sgl_buf.sgl = buf;
	ctx->user_buf.sgl_buf.num_sgls = num_sgls;

	if (nvme_sched_flag) {
		// Store all info in ctx before add to software queue
		ctx->tid = percpu_get(cpu_nr);
		ctx->fg_handle = fg_handle; 
		ctx->cmd = NVME_CMD_WRITE;
		ctx->req_cost = nvme_compute_req_cost(NVME_CMD_WRITE, lba_count * global_ns_sector_size);
		ctx->lba = lba;
		ctx->lba_count = lba_count;

		// add to SW queue
		struct nvme_sw_queue* swq = nvme_fgs[fg_handle].nvme_swq;
		ret = nvme_sw_queue_push_back(swq, ctx);
		if (ret != 0) {
			free_local_nvme_ctx(ctx);
			return -RET_NOMEM;
		}
	}
	else {
		ret = spdk_nvme_ns_cmd_writev(ns, percpu_get(qpair), lba, lba_count,
									  nvme_write_cb, ctx, 0, sgl_reset_cb, sgl_next_cb);
		if(ret != 0)
			printf("Writev failed: %lx %lx %lx\n", ret, num_sgls, lba_count);
		assert(ret == 0);
	}

	return RET_OK;
}

//TODO: use readv in reflex_server --> IO_CMD_PREADV aio command
long bsys_nvme_readv(hqu_t fg_handle, void __user **__restrict buf, int num_sgls,
		     unsigned long lba, unsigned int lba_count, unsigned long cookie)
{
	struct spdk_nvme_ns *ns;
	struct nvme_ctx *ctx;
	int ret;
	
	ctx = alloc_local_nvme_ctx();
	if (ctx == NULL) {
		printf("ERROR: Cannot allocate memory for nvme_ctx in bsys_nvme_read\n");
		return -RET_NOMEM;
	}
	ctx->cookie = cookie;
	ctx->user_buf.sgl_buf.sgl = buf;
	ctx->user_buf.sgl_buf.num_sgls = num_sgls;
	
	if (nvme_sched_flag) {
		// Store all info in ctx before add to software queue
		ctx->tid = RTE_PER_LCORE(cpu_nr);
		ctx->fg_handle = fg_handle; 
		ctx->cmd = NVME_CMD_READ;
		ctx->req_cost = nvme_compute_req_cost(NVME_CMD_READ, lba_count * global_ns_sector_size);
		ctx->lba = lba;
		ctx->lba_count = lba_count;

		// add to SW queue
		struct nvme_sw_queue* swq = nvme_fgs[fg_handle].nvme_swq;
		ret = nvme_sw_queue_push_back(swq, ctx);
		if (ret != 0) {
			free_local_nvme_ctx(ctx);
			printf("returning NOMEM from readv\n");
			return -RET_NOMEM;
		}
	}
	else {
		ret = spdk_nvme_ns_cmd_readv(ns, percpu_get(qpair), lba, lba_count,
									 nvme_read_cb, ctx, 0, sgl_reset_cb, sgl_next_cb);
		if(ret != 0)
			printf("Readv failed: %lx %lx %lx\n", ret, num_sgls, lba_count);
		assert(ret == 0);
	}
	
	return RET_OK;
}

unsigned long try_acquire_global_tokens(unsigned long token_demand) {
	unsigned long new_token_level = 0;
	unsigned long avail_tokens = 0;

	while (1) {
		avail_tokens = atomic_u64_read(&global_leftover_tokens);

		if (token_demand > avail_tokens) {
			if (atomic_u64_cmpxchg(&global_leftover_tokens, avail_tokens, 0)){
				return avail_tokens;
			}

		}
		else {
			new_token_level = avail_tokens - token_demand;
			if (atomic_u64_cmpxchg(&global_leftover_tokens, avail_tokens, new_token_level)){
				return token_demand;
			}
		}
	}
}

static void issue_nvme_req(struct nvme_ctx* ctx)
{
	int ret;
	struct nvme_tenant_mgmt* manager;
	struct iocb* iocb = &ctx->iocb;
	manager = &percpu_get(nvme_tenant_manager);

	//don't schedule request on flash if FAKE_FLASH test	
	if (nvme_dev_model == FAKE_FLASH) {
		if (ctx->cmd == NVME_CMD_READ) {
			usys_nvme_response(ctx->cookie, ctx->user_buf.buf, RET_OK);
			percpu_get(received_nvme_completions)++;
		}
		else if (ctx->cmd == NVME_CMD_WRITE) {
			usys_nvme_written(ctx->cookie, RET_OK);
			percpu_get(received_nvme_completions)++;
		}
		free_local_nvme_ctx(ctx);

		return; 
	}


	ret = io_submit(manager->aio_ctx, 1, &iocb);
	
	if (ret < 0) {
		printf("Error submitting nvme request: %s\n", strerror(errno));
		panic("Ran out of NVMe cmd buffer space\n");
	}
}


/*
 * nvme_sched_subround1: schedule latency critical tenant traffic 
 */
static inline int nvme_sched_subround1(void)
{
	struct nvme_tenant_mgmt* thread_tenant_manager;
	struct nvme_sw_queue* nvme_swq;
	struct nvme_ctx *ctx;
	unsigned long now;
	unsigned long time_delta;
	long POS_LIMIT = 0;
	unsigned long local_leftover = 0;
	unsigned long local_demand = 0;
	double token_increment;

	now = timer_now();	//in us
	time_delta = now - percpu_get(last_sched_time);
	percpu_get(last_sched_time) = now;
	
	thread_tenant_manager = &percpu_get(nvme_tenant_manager);
	
	list_for_each(&thread_tenant_manager->tenant_swq, nvme_swq, list) {
		// serve latency-critical (LC) tenants
		if (nvme_fgs[nvme_swq->fg_handle].latency_critical_flag) {
			if (nvme_swq->fg_handle == 2){
				//printf("%f\n", nvme_fgs[nvme_swq->fg_handle].scaled_IOPuS_limit);
			}
			
			token_increment = (nvme_fgs[nvme_swq->fg_handle].scaled_IOPuS_limit * time_delta) + 0.5; // 0.5 is for rounding
			nvme_swq->token_credit += (long) token_increment;
			if (nvme_swq->token_credit < -TOKEN_DEFICIT_LIMIT){
				/*
				 * Notify control plane, may need to re-negotiate tenant SLO
				 * FUTURE WORK: implement control plane
				 */

				//TODO: try to grab from global token bucket
				//NOTE: may also need to schedule LC tenants in round robin for fairness
			}
			while (nvme_sw_queue_isempty(nvme_swq) == 0 && 
				   nvme_swq->token_credit > -TOKEN_DEFICIT_LIMIT) {
				nvme_sw_queue_pop_front(nvme_swq, &ctx); 
				issue_nvme_req(ctx);
				nvme_swq->token_credit -= ctx->req_cost;
			}

			/*
			 * POS_LIMIT can be tuned to balance work-conservation and favoring of LC traffic
			 *	  * default POS_LIMIT    = 3 * token_increment
			 *	  						if LC tenant doesn't use tokens accumulated 
			 *	  						from ~3 sched rounds, donate them
			 *	  						
			 *   * lower POS_LIMIT 		is good for work-conservation 
			 *   						(give tokens to BE tenants more easily)
			 *   
			 *   * higher POS_LIMIT 	allows latency-critical tenants to accumulate 
			 *     						more tokens & burst
			 */
			POS_LIMIT = 3 * token_increment;
			if (nvme_swq->token_credit > POS_LIMIT) {
				local_leftover += (nvme_swq->token_credit * TOKEN_FRAC_GIVEAWAY);	
				nvme_swq->token_credit -= nvme_swq->token_credit * TOKEN_FRAC_GIVEAWAY; 
			}
		}
		else { // track demand of best-effort (will need for subround2)
			local_demand += nvme_swq->total_token_demand - nvme_swq->saved_tokens;
		}
	}

	
	percpu_get(local_extra_demand) = local_demand;
	percpu_get(local_leftover_tokens) = local_leftover;

	return 0;

}


/*
 * nvme_sched_subround2: schedule best-effort tenant traffic 
 */
static inline void nvme_sched_subround2(void)
{
	struct nvme_tenant_mgmt* thread_tenant_manager;
	struct nvme_sw_queue* nvme_swq;
	struct nvme_ctx *ctx;
	int i;
	unsigned long local_leftover = 0;
	unsigned long local_demand = 0;
	unsigned long be_tokens = 0;
	double token_increment = 0;
	unsigned long token_demand = 0;
	unsigned long global_tokens_acquired = 0;
	unsigned long now;
	unsigned long time_delta_cycles;


	local_leftover = percpu_get(local_leftover_tokens); 
	local_demand = percpu_get(local_extra_demand);

	thread_tenant_manager = &percpu_get(nvme_tenant_manager);
	
	// compare local leftover with local demand 
	// synchronize access to global token bucket
	if (local_leftover > 0 && local_demand == 0) { //give away leftoever tokens to global pool
		atomic_u64_fetch_and_add(&global_leftover_tokens, local_leftover);
		return;
	}
	else if (local_leftover < local_demand) { //try to get how much you need from global pool
		token_demand = local_demand - local_leftover;
		global_tokens_acquired = try_acquire_global_tokens(token_demand); // atomic 
		be_tokens = local_leftover + global_tokens_acquired;
	}
	else if (local_leftover >= local_demand) {
		be_tokens = local_leftover;
	}

	now = rdtsc();	
	time_delta_cycles = now - percpu_get(last_sched_time_be);
	percpu_get(last_sched_time_be) = now; 

	// serve best effort tenants in round-robin order
	// TODO: simplify by implementing separate per-thread lists of BE and LC tenants
	i = 0 ; 
	list_for_each(&thread_tenant_manager->tenant_swq, nvme_swq, list) {
		if (i < percpu_get(roundrobin_start)){
			i++;
			continue;
		}
		if (!nvme_fgs[nvme_swq->fg_handle].latency_critical_flag){ 
			be_tokens += nvme_sw_queue_take_saved_tokens(nvme_swq); 
			token_increment = (atomic_read(&global_be_token_rate_per_tenant) * time_delta_cycles) / (double) (cycles_per_us * 1E6);
			be_tokens += (long) (token_increment + 0.5);
					
			while ( (nvme_sw_queue_isempty(nvme_swq) == 0) && 
					nvme_sw_queue_peak_head_cost(nvme_swq) <= be_tokens) {
				nvme_sw_queue_pop_front(nvme_swq, &ctx); 
				issue_nvme_req(ctx);
				be_tokens -= ctx->req_cost;
			}
			//save extra tokens for this tenant if still has demand
			be_tokens -= nvme_sw_queue_save_tokens(nvme_swq, be_tokens);
			assert(be_tokens >= 0);
		}
		i++;
	}
	
	int j = 0;
	list_for_each(&thread_tenant_manager->tenant_swq, nvme_swq, list) {
		if (j >= percpu_get(roundrobin_start)){
			break;
		}
		log_debug("schedule tenant second %d\n", j);
		log_debug("subround2: sched tenant handle %ld, tenant_tokens %lu\n", nvme_swq->fg_handle, tenant_tokens);
		if (!nvme_fgs[nvme_swq->fg_handle].latency_critical_flag){		
			be_tokens += nvme_sw_queue_take_saved_tokens(nvme_swq); 
			token_increment = (atomic_read(&global_be_token_rate_per_tenant) * time_delta_cycles) / (double) (cycles_per_us * 1E6);
			be_tokens += (long) (token_increment + 0.5);
			
			while ( (nvme_sw_queue_isempty(nvme_swq) == 0) && 
					nvme_sw_queue_peak_head_cost(nvme_swq) <= be_tokens) { 
				nvme_sw_queue_pop_front(nvme_swq, &ctx); 
				issue_nvme_req(ctx);
				be_tokens -= ctx->req_cost; 
			}
			//save extra tokens for this tenant if still has demand
			be_tokens -= nvme_sw_queue_save_tokens(nvme_swq, be_tokens);
			assert(be_tokens >= 0);
		}
		j++;
	}

	int done = 0;
	if (thread_tenant_manager->num_best_effort_tenants > 0){
		while (1) { //find next round-robin start and check it's a best-effort tenant (otherwise unfair)
			percpu_get(roundrobin_start) = (percpu_get(roundrobin_start) + 1) % thread_tenant_manager->num_tenants;
			i = 0;
			list_for_each(&thread_tenant_manager->tenant_swq, nvme_swq, list) {
				if (i != percpu_get(roundrobin_start)){
					i++;
					continue;
				}
				if (!nvme_fgs[nvme_swq->fg_handle].latency_critical_flag){		
					done = 1; //incremented to next best effort tenant
				}
				break;
			}
			if (done == 1) { 
				break;
			}
		}
	}
	
	if (be_tokens > 0){
		atomic_u64_fetch_and_add(&global_leftover_tokens, be_tokens);
	}

}

/*
 * update_scheduler_bitvector: 
 * 		- synchronizes clearing of the global token bucket to limit global BE token accumulation
 * 		- mark global bitvector to indicate this thread has completed a scheduling round
 * 		- if last thread to complete a round, clear the global vector
 * 		- updates to global vector are not atomic operations because want to limit perf overhead
 * 		  and the exact timing of token bucket reset is not critical, as long as we reset approximately
 * 		  after each thread has had a chance to get tokens  
 */
static void update_scheduled_bitvector(void){
	
	int i;
	scheduled_bit_vector[RTE_PER_LCORE(cpu_nr)]++;

	for (i = 0; i < cpus_active; i++){
		if (scheduled_bit_vector[i] == 0)
			break;
	}
	if (i == cpus_active){ // all other threads scheduled at least once
		atomic_u64_write(&global_leftover_tokens, 0);
		
		//clear scheduled bit vector
		for (i = 0; i < cpus_active; i++) {
			scheduled_bit_vector[i] =  0;
		}
	}
	
}

int nvme_sched(void)
{
#ifdef NO_SCHED
	return 0;
#endif
	struct nvme_tenant_mgmt* thread_tenant_manager;
	thread_tenant_manager = &percpu_get(nvme_tenant_manager);
	
	if (thread_tenant_manager->num_tenants == 0) { 
		percpu_get(last_sched_time) = timer_now();
		percpu_get(last_sched_time_be) = rdtsc();
		update_scheduled_bitvector(); 
		return 0;
	}

	nvme_sched_subround1(); // serve latency-critical tenants
	nvme_sched_subround2(); // serve best-effort tenants
	
	percpu_get(local_leftover_tokens) = 0;
	percpu_get(local_extra_demand) = 0;

	update_scheduled_bitvector(); 

	return 0;
}


void
aio_write_cb(void *ctx)
{
	struct nvme_ctx *n_ctx = (struct nvme_ctx *) ctx;

	usys_nvme_written(n_ctx->cookie, RET_OK);
	
	free_local_nvme_ctx(n_ctx);
}

void
aio_read_cb(void *ctx)
{
	struct nvme_ctx *n_ctx = (struct nvme_ctx *) ctx;

	usys_nvme_response(n_ctx->cookie, n_ctx->iocb.u.c.buf, RET_OK);
	
	free_local_nvme_ctx(n_ctx);
}


void nvme_process_completions()
{
	int i;
	int max_completions = 4096;
	struct nvme_tenant_mgmt* manager;
	manager = &percpu_get(nvme_tenant_manager);
	struct io_event* events = manager->events;
	void* cookie;
	int count = 0;

	if (CFG.num_nvmedev == 0)
		return;

	for(i = 0; i < percpu_get(open_ev_ptr); i++) {
		usys_nvme_opened(percpu_get(open_ev[i]), global_ns_size, global_ns_sector_size);
		percpu_get(received_nvme_completions)++;
	}
	percpu_get(open_ev_ptr) = 0;
	count = io_getevents(manager->aio_ctx, 0, max_completions, manager->events, NULL);

	for(i =0; i < count; i++) {
		percpu_get(received_nvme_completions)++;
		cookie = events[i].data;
		if (events[i].obj->aio_lio_opcode == IO_CMD_PREAD){
			aio_read_cb(cookie);
		}
		else if (events[i].obj->aio_lio_opcode == IO_CMD_PWRITE){
			aio_write_cb(cookie);
		}
		else 
			panic("unknown completion!\n");
	}

}

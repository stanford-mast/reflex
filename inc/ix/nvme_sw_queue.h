/* 
 * Data structure for Flash SW queue scheduling
 * Lock-free and works for single producer, single consumer 
 *
*/

#include <ix/nvmedev.h>
#include <ix/list.h>

#define NVME_SW_QUEUE_SIZE (256*8)  

struct nvme_sw_queue
{
    struct nvme_ctx* buf[NVME_SW_QUEUE_SIZE]; 
	int count;				  // number of elements current in queue
    unsigned int head;       // head index (insert here)
    unsigned int tail;       // tail index (remove from here)
	unsigned long total_token_demand;
	unsigned long saved_tokens;
    long fg_handle;
	long token_credit;
	struct list_node list;
};



void nvme_sw_queue_init(struct nvme_sw_queue *q, long fg_handle);
int nvme_sw_queue_push_back(struct nvme_sw_queue *q, struct nvme_ctx *ctx);
int nvme_sw_queue_pop_front(struct nvme_sw_queue *q, struct nvme_ctx **ctx);
int nvme_sw_queue_isempty(struct nvme_sw_queue *q);
int nvme_sw_queue_peak_head_cost(struct nvme_sw_queue *q);
unsigned long nvme_sw_queue_save_tokens(struct nvme_sw_queue *q, unsigned long tokens);
unsigned long nvme_sw_queue_take_saved_tokens(struct nvme_sw_queue *q);

double nvme_sw_queue_save_tokens_fraction(struct nvme_sw_queue *q, double tokens);
double nvme_sw_queue_take_saved_tokens_fraction(struct nvme_sw_queue *q);

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

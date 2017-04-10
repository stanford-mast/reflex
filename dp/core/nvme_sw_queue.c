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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ix/nvme_sw_queue.h>
#include <ix/errno.h>
#include <ix/log.h>
#include <ix/timer.h>


void nvme_sw_queue_init(struct nvme_sw_queue *q, long fg_handle)
{
	q->count = 0;
    q->head = 0;
    q->tail = 0;
	q->total_token_demand = 0;
	q->saved_tokens = 0;
	q->token_credit = 0;
	q->fg_handle = fg_handle;
}

int nvme_sw_queue_push_back(struct nvme_sw_queue *q, struct nvme_ctx *ctx)
{
    if(q->count == NVME_SW_QUEUE_SIZE){ 
		log_info("nvme_sw_queue full!\n");
		return -EAGAIN;
	}
	q->buf[q->head] = ctx;
   	q->head = (q->head + 1) % NVME_SW_QUEUE_SIZE; 
    q->count++;
	q->total_token_demand += ctx->req_cost;
	return 0;
}

int nvme_sw_queue_pop_front(struct nvme_sw_queue *q, struct nvme_ctx **ctx)
{
    if(q->count == 0){
		//log_info("ringbuf empty!\n");
        return -EAGAIN;
	}
	*ctx = q->buf[q->tail];
	q->total_token_demand -= q->buf[q->tail]->req_cost;
    q->tail = (q->tail + 1) % NVME_SW_QUEUE_SIZE; 
    q->count--;
	return 0;
}

int nvme_sw_queue_isempty(struct nvme_sw_queue *q)
{
	if (q->count == 0) {
		return 1;
	}
	else {
		return 0;
	}
}

int nvme_sw_queue_peak_head_cost(struct nvme_sw_queue *q)
{
	if (q->count == 0)
		return -1;

	return q->buf[q->tail]->req_cost;

}

unsigned long nvme_sw_queue_save_tokens(struct nvme_sw_queue *q, unsigned long tokens)
{

	//only save tokens up to how much have demand, return the rest
	if (q->total_token_demand == 0) {
		q->saved_tokens = 0;
		return 0; 
	}
	if (q->total_token_demand > tokens) {	
		q->saved_tokens += tokens;
		return tokens;
	}
	if (q->total_token_demand <= tokens) {	
		q->saved_tokens += q->total_token_demand;
		return q->total_token_demand;
	}
	return 0;
}


unsigned long nvme_sw_queue_take_saved_tokens(struct nvme_sw_queue *q)
{
	unsigned long saved_tokens = q->saved_tokens;
	q->saved_tokens = 0;	
	return saved_tokens;
}


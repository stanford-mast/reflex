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
 * ixev.h - a library that behaves mostly like libevent.
 */

#pragma once

#include "ix.h"
#include <stdio.h>

/* FIXME: we won't need recv depth when i get a chance to fix the kernel */
#define IXEV_RECV_DEPTH	1024
#define IXEV_SEND_DEPTH	16

struct ixev_ctx;
struct ixev_nvme_ioq_ctx;
struct ixev_nvme_req_ctx;
struct ixev_ref;
struct ixev_buf;

/*
 * FIXME: right now we only support some libevent features
 * - add level triggered support (now only edge triggered)
 * - add one fire events (now only persistent)
 * - add timeout support for events
 */

/* IX event types */
#define IXEVHUP		0x1 /* the connection was closed (or failed) */
#define IXEVIN		0x2 /* new data is available for reading */
#define IXEVOUT		0x4 /* more space is available for writing */
#define IXEV_NVME_RD		0x8 /* nvme data ready for reading */
#define IXEV_NVME_WR		0x10 /* nvme write completed */

struct ixev_conn_ops {
	struct ixev_ctx * (*accept) (struct ip_tuple *id);
	void		  (*release) (struct ixev_ctx *ctx);
	void		  (*dialed) (struct ixev_ctx *ctx, long ret);
};

//FIXME: not sure what kind of ops want here!!!
struct ixev_nvme_ops {
	void (*opened) (hqu_t handle, unsigned long ns_size, unsigned long ns_sector_size); //???
	void (*registered_flow) (long flow_group_id, struct ixev_ctx* ctx, long ret); //???
	void (*unregistered_flow) (long flow_group_id, long ret); //???
};

/*
 * Use this callback to receive network event notifications
 */
typedef void (*ixev_handler_t)(struct ixev_ctx *ctx, unsigned int reason);

/*
 * Use this callback to receive nvme event notifications
 */
typedef void (*ixev_nvme_handler_t)(struct ixev_nvme_req_ctx *ctx, unsigned int reason);

/*
 * Use this callback to free memory after zero copy sends
 */
typedef void (*ixev_sent_cb_t)(struct ixev_ref *ref);

struct ixev_ref {
	ixev_sent_cb_t	cb;	  /* the decrement ref callback function */
	size_t		send_pos; /* the number of bytes sent before safe to free */
	struct ixev_ref	*next;    /* the next ref in the sequence */
};

struct ixev_ctx {
	hid_t		handle;			/* the IX flow handle */
	unsigned long	user_data;		/* application data */
	uint64_t	generation;		/* generation number */
	ixev_handler_t	handler;		/* the event handler */
	unsigned int	en_mask;		/* a mask of enabled events */
	unsigned int	trig_mask;		/* a mask of triggered events */
	uint16_t	recv_head;		/* received data SG head */
	uint16_t	recv_tail;		/* received data SG tail */
	uint16_t	send_count;		/* the current send SG count */
	uint16_t	is_dead: 1;		/* is the connection dead? */

	size_t		send_total;		/* the total requested bytes */
	size_t		sent_total;		/* the total completed bytes */
	struct ixev_ref	*ref_head;		/* list head of references */
	struct ixev_ref *ref_tail;		/* list tail of references */
	struct ixev_buf *cur_buf;		/* current buffer */

	struct bsys_desc *recv_done_desc;	/* the current recv_done bsys descriptor */
	struct bsys_desc *sendv_desc;		/* the current sendv bsys descriptor */

	struct sg_entry	recv[IXEV_RECV_DEPTH];	/* receieve SG array */
	struct sg_entry send[IXEV_SEND_DEPTH];	/* send SG array */
};

enum io_type {
	READ,			//0
	WRITE,			//1
	NUM_IO_TYPES	//2
};

struct ixev_nvme_ioq_ctx {
	hqu_t 			handle;
	ixev_nvme_handler_t	handler;		/* the event handler */
	unsigned int	en_mask;		/* a mask of enabled events */
	unsigned int	trig_mask;		/* a mask of triggered events */
	unsigned long 	curr_queue_depth;
};
//FIXME: not sure of relationship between ioq and req contexts!!!
struct ixev_nvme_req_ctx {
	struct ixev_nvme_ioq_ctx *ctx;
	hqu_t 			handle;			/* the queue handle submitting this request to */
	ixev_nvme_handler_t	handler;	/* the event handler */
	unsigned int	en_mask;		/* a mask of enabled events */
	unsigned int	trig_mask;		/* a mask of triggered events */
	char buf[];
};

static inline void ixev_check_hacks(struct ixev_ctx *ctx)
{
	/*
	 * Temporary hack:
	 *
	 * FIXME: we need to flush commands in batches to limit our
	 * command buffer size. Then this restriction can be lifted.
	 */
	if (unlikely(karr->len >= karr->max_len)) {
		printf("ixev: ran out of command space\n");
		exit(-1);
	}
}

extern ssize_t ixev_recv(struct ixev_ctx *ctx, void *addr, size_t len);
extern void *ixev_recv_zc(struct ixev_ctx *ctx, size_t len);
extern ssize_t ixev_send(struct ixev_ctx *ctx, void *addr, size_t len);
extern ssize_t ixev_send_zc(struct ixev_ctx *ctx, void *addr, size_t len);
extern void ixev_add_sent_cb(struct ixev_ctx *ctx, struct ixev_ref *ref);

extern void ixev_close(struct ixev_ctx *ctx);

extern void ixev_nvme_open(long dev_id, long ns_id);
extern void ixev_nvme_read(hqu_t handle, void * buf, unsigned long lba,
			   unsigned int lba_count, unsigned long cookie);
extern void ixev_nvme_write(hqu_t handle, void *buf, unsigned long lba,
			    unsigned int lba_count, unsigned long cookie);
extern void ixev_nvme_readv(hqu_t fg_handle, void **sgls,
			    int num_sgls, unsigned long lba, unsigned int lba_count,
			    unsigned long cookie);
extern void ixev_nvme_writev(hqu_t fg_handle, void **sgls,
			     int num_sgls, unsigned long lba, unsigned int lba_count,
			     unsigned long cookie);

extern void ixev_nvme_register_flow(long flow_group_id, unsigned long cookie, unsigned int latency_us_SLO,
							 unsigned long IOPS_SLO, int rw_ratio_SLO);
extern void ixev_nvme_unregister_flow(long flow_group_id); 


/**
 * ixev_dial - open a connection
 * @ctx: a freshly allocated and initialized context
 * @id: the address and port
 * @cb: the completion callback
 *
 * The completion returns a handle, or <0 if there was
 * an error.
 */
static inline void
ixev_dial(struct ixev_ctx *ctx, struct ip_tuple *id)
{
	struct bsys_desc *d = __bsys_arr_next(karr);
	ixev_check_hacks(ctx);

	ksys_tcp_connect(d, id, (unsigned long) ctx);
}

extern void ixev_ctx_init(struct ixev_ctx *ctx);
extern void ixev_nvme_ioq_ctx_init(struct ixev_nvme_ioq_ctx *ctx);
extern void ixev_nvme_req_ctx_init(struct ixev_nvme_req_ctx *ctx);
extern void ixev_wait(void);

extern void ixev_set_handler(struct ixev_ctx *ctx, unsigned int mask,
			     ixev_handler_t handler);
extern void ixev_set_nvme_handler(struct ixev_nvme_req_ctx *ctx, unsigned int mask,
			     ixev_nvme_handler_t handler);

extern int ixev_init_thread(void);
extern int ixev_init(struct ixev_conn_ops *ops);
extern int ixev_init_nvme(struct ixev_nvme_ops *ops);
extern int ixev_init_conn_nvme(struct ixev_conn_ops *conn_ops, struct ixev_nvme_ops *nvme_ops);

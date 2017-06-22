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
 * ix.h - the main libix header
 */

#pragma once

#include "libix_syscall.h"

struct ix_ops {
	void (*udp_recv)(void *addr, size_t len, struct ip_tuple *id);
	void (*udp_sent)(unsigned long cookie);
	void (*tcp_connected)(hid_t handle, unsigned long cookie,
			      long ret);
	void (*tcp_knock)(hid_t handle, struct ip_tuple *id);
	void (*tcp_recv)(hid_t handle, unsigned long cookie,
			 void *addr, size_t len);
	void (*tcp_sent)(hid_t handle, unsigned long cookie,
			 size_t win_size);
	void (*tcp_dead)(hid_t handle, unsigned long cookie);
	void (*nvme_written)    (unsigned long cookie, long ret);
	void (*nvme_response)   (unsigned long cookie, void *buf, long ret);
	void (*nvme_opened)     (hqu_t handle, unsigned long ns_size, unsigned long ns_sector_size);
	void (*nvme_registered_flow)   (long flow_group_id, unsigned long cookie, long ret);
	void (*nvme_unregistered_flow)     (long flow_group_id, long ret);
	void (*timer_event)(unsigned long cookie);
};

extern void ix_flush(void);
extern __thread struct bsys_arr *karr;

static inline int ix_bsys_idx(void)
{
	return karr->len;
}

static inline void ix_udp_send(void *addr, size_t len, struct ip_tuple *id,
			       unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_udp_send(__bsys_arr_next(karr), addr, len, id, cookie);
}

static inline void ix_udp_sendv(struct sg_entry *ents, unsigned int nrents,
				struct ip_tuple *id, unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_udp_sendv(__bsys_arr_next(karr), ents, nrents, id, cookie);
}

static inline void ix_udp_recv_done(void *addr)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_udp_recv_done(__bsys_arr_next(karr), addr);
}

static inline void ix_tcp_connect(struct ip_tuple *id, unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_connect(__bsys_arr_next(karr), id, cookie);
}

static inline void ix_tcp_accept(hid_t handle, unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_accept(__bsys_arr_next(karr), handle, cookie);
}

static inline void ix_tcp_reject(hid_t handle)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_reject(__bsys_arr_next(karr), handle);
}

static inline void ix_tcp_send(hid_t handle, void *addr, size_t len)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_send(__bsys_arr_next(karr), handle, addr, len);
}

static inline void ix_tcp_sendv(hid_t handle, struct sg_entry *ents,
				unsigned int nrents)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_sendv(__bsys_arr_next(karr), handle, ents, nrents);
}

static inline void ix_tcp_recv_done(hid_t handle, size_t len)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_recv_done(__bsys_arr_next(karr), handle, len);
}

static inline void ix_tcp_close(hid_t handle)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_tcp_close(__bsys_arr_next(karr), handle);
}

static inline void ix_nvme_open(long dev_id, long ns_id)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_nvme_open(__bsys_arr_next(karr), dev_id, ns_id);
}
	
static inline void ix_nvme_write(hqu_t handle, void *buf, unsigned long lba,
				 unsigned int lba_count, unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_nvme_write(__bsys_arr_next(karr), handle, buf, lba, lba_count, cookie);
}

static inline void ix_nvme_read(hqu_t handle, void * buf, unsigned long lba,
				unsigned int lba_count, unsigned long cookie) 
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_nvme_read(__bsys_arr_next(karr), handle, buf, lba, lba_count, cookie);
}

static inline void ix_nvme_readv(hqu_t fg_handle, void **sgls,
				  int num_sgls, unsigned long lba, unsigned int lba_count,
				  unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_nvme_readv(__bsys_arr_next(karr), fg_handle, sgls, num_sgls,
			 lba, lba_count, cookie);
}

static inline void ix_nvme_writev(hqu_t fg_handle, void **sgls,
				  int num_sgls, unsigned long lba, unsigned int lba_count,
				  unsigned long cookie)
{
	if (karr->len >= karr->max_len)
		ix_flush();

	ksys_nvme_writev(__bsys_arr_next(karr), fg_handle, sgls, num_sgls,
			 lba, lba_count, cookie);
}


extern void *ix_alloc_pages(int nrpages);
extern void ix_free_pages(void *addr, int nrpages);

extern void ix_handle_events(void);
extern int ix_poll(void);
extern int ix_init(struct ix_ops *ops, int batch_depth);


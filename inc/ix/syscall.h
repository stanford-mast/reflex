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
 * syscall.h - system call support (regular and batched)
 */

#pragma once
#include <rte_per_lcore.h>

#include <ix/compiler.h>
#include <ix/types.h>
#include <ix/cpu.h>
#include <ix/log.h>
#define SYSCALL_START	0x100000


/*
 * Data structures used as arguments.
 */

struct ip_tuple {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint16_t src_port;
	uint16_t dst_port;
} __packed;

struct sg_entry {
	void *base;
	size_t len;
} __packed;

#define MAX_SG_ENTRIES	30

typedef long hid_t;
typedef long hqu_t;

enum {
	HIGH_PRIORITY = 0,
	LOW_PRIORITY = 1,
	NUM_PRIORITY_LEVELS = 2,
};

enum {
	RET_OK		= 0, /* Successful                 */
	RET_NOMEM	= 1, /* Out of memory              */
	RET_NOBUFS	= 2, /* Out of buffer space        */
	RET_INVAL	= 3, /* Invalid parameter          */
	RET_AGAIN	= 4, /* Try again later            */
	RET_FAULT	= 5, /* Bad memory address         */
	RET_NOSYS	= 6, /* System call does not exist */
	RET_NOTSUP	= 7, /* Operation is not supported */
	RET_BADH	= 8, /* An invalid handle was used */
	RET_CLOSED	= 9, /* The connection is closed   */
	RET_CONNREFUSED = 10, /* Connection refused        */
	RET_CANTMEETSLO = 11, /* Cannot satisfy Flash SLO */
s};


/*
 * System calls
 */
enum {
	SYS_BPOLL = 0,
	SYS_BCALL,
	SYS_BADDR,
	SYS_MMAP,
	SYS_MUNMAP,
	SYS_SPAWNMODE,
	SYS_NRCPUS,
	SYS_TIMER_INIT,
	SYS_TIMER_CTL,
	SYS_NR,
};


/*
 * Batched system calls
 */

typedef long(*bsysfn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

/*
 * batched system call descriptor format:
 * sysnr: - the system call number
 * arga-arge: parameters one through five
 * argd: overwritten with the return code
 */
struct bsys_desc {
	uint64_t sysnr;
	uint64_t arga, argb, argc, argd, arge, argf;
} __packed;

struct bsys_ret {
	uint64_t sysnr;
	uint64_t cookie;
	long ret;
	uint64_t pad[4];
} __packed;

#define BSYS_DESC_NOARG(desc, vsysnr) \
	(desc)->sysnr = (uint64_t) (vsysnr)
#define BSYS_DESC_1ARG(desc, vsysnr, varga) \
	BSYS_DESC_NOARG(desc, vsysnr),      \
	(desc)->arga = (uint64_t) (varga)
#define BSYS_DESC_2ARG(desc, vsysnr, varga, vargb) \
	BSYS_DESC_1ARG(desc, vsysnr, varga),       \
	(desc)->argb = (uint64_t) (vargb)
#define BSYS_DESC_3ARG(desc, vsysnr, varga, vargb, vargc) \
	BSYS_DESC_2ARG(desc, vsysnr, varga, vargb),       \
	(desc)->argc = (uint64_t) (vargc)
#define BSYS_DESC_4ARG(desc, vsysnr, varga, vargb, vargc, vargd) \
	BSYS_DESC_3ARG(desc, vsysnr, varga, vargb, vargc),       \
	(desc)->argd = (uint64_t) (vargd)
#define BSYS_DESC_5ARG(desc, vsysnr, varga, vargb, vargc, vargd, varge)	\
	BSYS_DESC_4ARG(desc, vsysnr, varga, vargb, vargc, vargd),		\
	(desc)->arge = (uint64_t) (varge)
#define BSYS_DESC_6ARG(desc, vsysnr, varga, vargb, vargc, vargd, varge, vargf) \
	BSYS_DESC_5ARG(desc, vsysnr, varga, vargb, vargc, vargd, varge),	\
	(desc)->argf = (uint64_t) (vargf)

struct bsys_arr {
	unsigned long len;
	unsigned long max_len;
	struct bsys_desc descs[];
};

/**
 * __bsys_arr_next - get the next free descriptor
 * @a: the syscall array
 *
 * Returns a descriptor, or NULL if none are available.
 */
static inline struct bsys_desc *__bsys_arr_next(struct bsys_arr *a)
{
	return &a->descs[a->len++];
}

/**
 * bsys_arr_next - get the next free descriptor, checking for overflow
 * @a: the syscall array
 *
 * Returns a descriptor, or NULL if none are available.
 */
static inline struct bsys_desc *bsys_arr_next(struct bsys_arr *a)
{
	if (a->len >= a->max_len)
		return NULL;

	return __bsys_arr_next(a);
}


/*
 * Commands that can be sent from the user-level application to the kernel.
 */

enum {
	KSYS_UDP_SEND = 0,
	KSYS_UDP_SENDV,
	KSYS_UDP_RECV_DONE,
	KSYS_TCP_CONNECT,
	KSYS_TCP_ACCEPT,
	KSYS_TCP_REJECT,
	KSYS_TCP_SEND,
	KSYS_TCP_SENDV,
	KSYS_TCP_RECV_DONE,
	KSYS_TCP_CLOSE,
	KSYS_NVME_WRITE,
	KSYS_NVME_READ,
	KSYS_NVME_WRITEV,
	KSYS_NVME_READV,
	KSYS_NVME_OPEN,
	KSYS_NVME_CLOSE,
	KSYS_NVME_REGISTER_FLOW,
	KSYS_NVME_UNREGISTER_FLOW,
	KSYS_NR,
};

/**
 * ksys_udp_send - transmits a UDP packet
 * @d: the syscall descriptor to program
 * @addr: the address of the packet data
 * @len: the length of the packet data
 * @id: the UDP 4-tuple
 * @cookie: a user-level tag for the request
 */
static inline void ksys_udp_send(struct bsys_desc *d, void *addr,
				 size_t len, struct ip_tuple *id,
				 unsigned long cookie)
{
	BSYS_DESC_4ARG(d, KSYS_UDP_SEND, addr, len, id, cookie);
}

/**
 * ksys_udp_sendv - transmits a UDP packet using scatter-gather data
 * @d: the syscall descriptor to program
 * @ents: the scatter-gather vector array
 * @nrents: the number of scatter-gather vectors
 * @cookie: a user-level tag for the request
 * @id: the UDP 4-tuple
 */
static inline void ksys_udp_sendv(struct bsys_desc *d,
				  struct sg_entry *ents,
				  unsigned int nrents,
				  struct ip_tuple *id,
				  unsigned long cookie)
{
	BSYS_DESC_4ARG(d, KSYS_UDP_SENDV, ents, nrents, id, cookie);
}

/**
 * ksys_udp_recv_done - inform the kernel done using a UDP packet buffer
 * @d: the syscall descriptor to program
 * @iomap: an address anywhere inside the mbuf
 *
 * NOTE: Calling this function allows the kernel to free mbuf's when
 * the application has finished using them.
 */
static inline void ksys_udp_recv_done(struct bsys_desc *d, void *iomap)
{
	BSYS_DESC_1ARG(d, KSYS_UDP_RECV_DONE, iomap);
}

/**
 * ksys_tcp_connect - create a TCP connection
 * @d: the syscall descriptor to program
 * @id: the TCP 4-tuple
 * @cookie: a user-level tag for the flow
 */
static inline void
ksys_tcp_connect(struct bsys_desc *d, struct ip_tuple *id,
		 unsigned long cookie)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_CONNECT, id, cookie);
}

/**
 * ksys_tcp_accept - accept a TCP connection request
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 */
static inline void
ksys_tcp_accept(struct bsys_desc *d, hid_t handle, unsigned long cookie)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_ACCEPT, handle, cookie);
}

/**
 * ksys_tcp_reject - reject a TCP connection request
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 */
static inline void
ksys_tcp_reject(struct bsys_desc *d, hid_t handle)
{
	BSYS_DESC_1ARG(d, KSYS_TCP_REJECT, handle);
}

/**
 * ksys_tcp_send - send data on a TCP flow
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @addr: the address of the data
 * @len: the length of the data
 */
static inline void
ksys_tcp_send(struct bsys_desc *d, hid_t handle,
	      void *addr, size_t len)
{
	BSYS_DESC_3ARG(d, KSYS_TCP_SEND, handle, addr, len);
}

/**
 * ksys_tcp_sendv - send scatter-gather data on a TCP flow
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @ents: an array of scatter-gather vectors
 * @nrents: the number of scatter-gather vectors
 */
static inline void
ksys_tcp_sendv(struct bsys_desc *d, hid_t handle,
	       struct sg_entry *ents, unsigned int nrents)
{
	BSYS_DESC_3ARG(d, KSYS_TCP_SENDV, handle, ents, nrents);
}

/**
 * ksys_tcp_recv_done - acknowledge the receipt of TCP data buffers
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 * @len: the number of bytes to acknowledge
 *
 * NOTE: This function is used to free memory and to adjust
 * the receive window.
 */
static inline void
ksys_tcp_recv_done(struct bsys_desc *d, hid_t handle, size_t len)
{
	BSYS_DESC_2ARG(d, KSYS_TCP_RECV_DONE, handle, len);
}

/**
 * ksys_tcp_close - closes a TCP connection
 * @d: the syscall descriptor to program
 * @handle: the TCP flow handle
 */
static inline void
ksys_tcp_close(struct bsys_desc *d, hid_t handle)
{
	BSYS_DESC_1ARG(d, KSYS_TCP_CLOSE, handle);
}

/**
 * ksys_nvme_open - opens a queue pair for a namesapce on a nvme device
 * @dev_id: The device id of the nvme device
 * @ns_id: The namespace on the nvme device  
 */
static inline void
ksys_nvme_open(struct bsys_desc *d, long dev_id, long ns_id)
{
	BSYS_DESC_2ARG(d, KSYS_NVME_OPEN, dev_id, ns_id);
}


/**
 * ksys_nvme_close - close a queue pair for a namesapce on a nvme device
 * @dev_id: the device id of the nvme device
 * @ns_id: the namespace on the nvme device  
 * @handle: the handle for the queue to close
 */
static inline void
ksys_nvme_close(struct bsys_desc *d, long dev_id, long ns_id, hqu_t handle)
{
	BSYS_DESC_3ARG(d, KSYS_NVME_CLOSE, dev_id, ns_id, handle);
}

/**
 * ksys_nvme_write - writes to an nvme queue
 * @d: the syscal descriptor to program
 * @priority: The nvme request priority (based on flow)
 * @buf: the user space buffer to read from
 * @addr: the nvme address of the data
 * @len: size of the write in bytes
 * @cookie: a user-level tag for the request
 */
static inline void
ksys_nvme_write(struct bsys_desc *d, hqu_t priority, void *buf,
		unsigned long lba, unsigned int lba_count, unsigned long cookie)
{
	BSYS_DESC_5ARG(d, KSYS_NVME_WRITE, priority, buf, lba, lba_count, cookie);
}

/**
 * ksys_nvme_read - reads from an nvme queue
 * @d: the syscal descriptor to program
 * @priority: The nvme request priority (based on flow)
 * @buf: the user space buffer to write to
 * @addr: the nvme address of the data
 * @len: site of the read in bytes
 * @cookie: a user-level tag for the response
 */
static inline void
ksys_nvme_read(struct bsys_desc *d, hqu_t priority, void * buf,
	       unsigned long lba, unsigned int lba_count, unsigned long cookie)
{
	BSYS_DESC_5ARG(d, KSYS_NVME_READ, priority, buf, lba, lba_count, cookie);
}

/**
 * ksys_nvme_writev - vectored (gather) write to an nvme queue
 * @d: the syscal descriptor to program
 * @priority: The nvme request priority (based on flow)
 * @sgls: the scatter/gather list
 * @num_sgls: number of items in the sgl
 * @addr: the nvme address of the data
 * @len: size of the write in bytes
 * @cookie: a user-level tag for the request
 */
static inline void
ksys_nvme_writev(struct bsys_desc *d, hqu_t priority, void **sgls,
		 int num_sgls, unsigned long lba, int lba_count, unsigned long cookie)
{
	BSYS_DESC_6ARG(d, KSYS_NVME_WRITEV, priority, sgls, num_sgls,
		       lba, lba_count, cookie);
}

/**
 * ksys_nvme_readv - vectored (scatter) read to an nvme queue
 * @d: the syscal descriptor to program
 * @priority: The nvme request priority (based on flow)
 * @sgls: the scatter/gather list
 * @num_sgls: number of items in the sgl
 * @addr: the nvme address of the data
 * @len: size of the write in bytes
 * @cookie: a user-level tag for the request
 */
static inline void
ksys_nvme_readv(struct bsys_desc *d, hqu_t fg_handle, void **sgls, int num_sgls,
		unsigned long lba, int lba_count, unsigned long cookie)
{
	BSYS_DESC_6ARG(d, KSYS_NVME_READV, fg_handle, sgls, num_sgls,
		       lba, lba_count, cookie);
}


/**
 * ksys_nvme_register_flow - registers an nvme flow
 * @d: the syscal descriptor to program
 * @flow_group_id: flow group's id
 * @ns_id: namespace id
 * @latency_us_SLO: latency SLO (0 if not latency critical, ie if best-effort)
 * @IOPS_SLO: IOPS SLO (0 if not latency critical)
 * @rw_ratio_SLO: read write ratio corresponding to SLO above
 */
static inline void
ksys_nvme_register_flow(struct bsys_desc *d, long flow_group_id, unsigned long cookie, 
							 unsigned int latency_us_SLO, unsigned long IOPS_SLO, 
							 int rw_ratio_SLO  )
{
	BSYS_DESC_5ARG(d, KSYS_NVME_REGISTER_FLOW, flow_group_id, cookie, 
				   latency_us_SLO, IOPS_SLO, rw_ratio_SLO); 
}

/* ksys_nvme_unregister_flow - unregisters an nvme flow
 * @d: the syscal descriptor to program
 * @fg_handle: fg_handle for freed flow
 */
static inline void
ksys_nvme_unregister_flow(struct bsys_desc *d, long fg_handle)
{
	BSYS_DESC_1ARG(d, KSYS_NVME_UNREGISTER_FLOW, fg_handle);
}


/*
 * Commands that can be sent from the kernel to the user-level application.
 */

enum {
	USYS_UDP_RECV = 0,
	USYS_UDP_SENT,
	USYS_TCP_CONNECTED,
	USYS_TCP_KNOCK,
	USYS_TCP_RECV,
	USYS_TCP_SENT,
	USYS_TCP_DEAD,
	USYS_NVME_WRITTEN,
	USYS_NVME_RESPONSE,
	USYS_NVME_OPENED,
	USYS_NVME_CLOSED,
	USYS_NVME_REGISTERED_FLOW,
	USYS_NVME_UNREGISTERED_FLOW,
	USYS_TIMER,
	USYS_NR,
};


RTE_DECLARE_PER_LCORE(struct bsys_arr *, usys_arr);
RTE_DECLARE_PER_LCORE(unsigned long, syscall_cookie);

/**
 * usys_reset - reset the batched call array
 *
 * Call this before batching system calls.
 */
static inline void usys_reset(void)
{
	percpu_get(usys_arr)->len = 0;
}

/**
 * usys_next - get the next batched syscall descriptor
 *
 * Returns a syscall descriptor.
 */
static inline struct bsys_desc *usys_next(void)
{
	return __bsys_arr_next(percpu_get(usys_arr));
}

/**
 * usys_udp_recv - receive a UDP packet
 * @addr: the address of the packet data
 * @len: the length of the packet data
 * @id: the UDP 4-tuple
 */
static inline void usys_udp_recv(void *addr, size_t len, struct ip_tuple *id)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_UDP_RECV, addr, len, id);
}

/**
 * usys_udp_sent - Notifies the user that a UDP packet send completed
 * @cookie: a user-level token for the request
 *
 * NOTE: Calling this function allows the user application to unpin memory
 * that was locked for zero copy transfer. Acknowledgements are always in
 * FIFO order.
 */
static inline void usys_udp_sent(unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_1ARG(d, USYS_UDP_SENT, cookie);
}

/**
 * usys_tcp_connected - Notifies the user that an outgoing connection attempt
 *			has completed. (not necessarily successfully)
 * @handle: the TCP flow handle
 * @cookie: a user-level token
 * @ret: the result (return code)
 */
static inline void
usys_tcp_connected(hid_t handle, unsigned long cookie, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_TCP_CONNECTED, handle, cookie, ret);
}

/**
 * usys_tcp_knock - Notifies the user that a remote host is
 *                  trying to open a connection
 * @handle: the TCP flow handle
 * @id: the TCP 4-tuple
 */
static inline void
usys_tcp_knock(hid_t handle, struct ip_tuple *id)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_TCP_KNOCK, handle, id);
}

/**
 * usys_tcp_recv - receive TCP data
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 * @addr: the address of the received data
 * @len: the length of the received data
 */
static inline void
usys_tcp_recv(hid_t handle, unsigned long cookie, void *addr, size_t len)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_4ARG(d, USYS_TCP_RECV, handle, cookie, addr, len);
}

/**
 * usys_tcp_sent - indicates transmission is finished
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 * @len: the length in bytes sent
 *
 * Typically, an application will use this notifier to unreference buffers
 * and to send more pending data.
 */
static inline void
usys_tcp_sent(hid_t handle, unsigned long cookie, size_t len)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_TCP_SENT, handle, cookie, len);
}

/**
 * usys_tcp_dead - indicates that the remote host has closed the connection
 * @handle: the TCP flow handle
 * @cookie: a user-level tag for the flow
 *
 * NOTE: the user must still close the connection on the local end
 * using ksys_tcp_close().
 */
static inline void
usys_tcp_dead(hid_t handle, unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_TCP_DEAD, handle, cookie);
}

/**
 * usys_nvme_written - indicates transmission is finished, not necessarily success
 * @cookie: a user-level tag for the flow
 * @ret: return status of nvme write
 *
 * Typically, an application will use this notifier to unreference buffers
 * and to send more pending data.
 */
static inline void
usys_nvme_written(unsigned long cookie, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_NVME_WRITTEN, cookie, ret);
}

/**
 * usys_nvme_response - delivers read response into buf, not necessarily successful
 * @cookie: a user-level tag for the flow
 * @buf: the receive data buffer
 * @ret: return status of nvme read 
 *
 * Typically, an application will use this to receive requested data
 */
static inline void
usys_nvme_response(unsigned long cookie, void *buf, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_NVME_RESPONSE, cookie, buf, ret);
}

/**
 * usys_nvme_opened - indicates that an attempt to open a queue to a
 * device has completed - not necessarily successful 
 * @handle: the nvme queue handle
 * @size: the size of the opened namespace
 * @sector size: the sector size of the opened namespace
 */
static inline void
usys_nvme_opened(hqu_t handle, long ns_size, long ns_sector_size)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_NVME_OPENED, handle, ns_size, ns_sector_size);
}


/**
 * usys_nvme_closed - indicates that an nvme queue has (attempted to be) closed 
 * 					- attempt may not necessarily be successful 
 * @handle: the nvme queue handle
 * @ret: the result (return code) 
 */
static inline void
usys_nvme_closed(hqu_t handle, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_NVME_CLOSED, handle, ret);
}

/**
 * usys_nvme_registered_flow - indicatest that registered flow 
 * @flow_group_id: the flow group's id
 * @ret: the result (return code) 
 */
static inline void
usys_nvme_registered_flow(long fg_handle, unsigned long cookie, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_3ARG(d, USYS_NVME_REGISTERED_FLOW, fg_handle, cookie, ret);
}

/**
 * usys_nvme_unregistered_flow - indicates that an nvme flow group was unregistered 
 * @flow_group_id: the flow group's id
 * @ret: the result (return code) 
 */
static inline void
usys_nvme_unregistered_flow(long flow_group_id, long ret)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_2ARG(d, USYS_NVME_UNREGISTERED_FLOW, flow_group_id, ret);
}

/*
 * usys_timer - indicates that there is a timer event
 */
static inline void
usys_timer(unsigned long cookie)
{
	struct bsys_desc *d = usys_next();
	BSYS_DESC_1ARG(d, USYS_TIMER, cookie);
}

static inline void sys_test_ix()
{
	log_info("\n************************PRINTITNG FROM SYS TEST**********************\n");
}

int sys_bpoll(struct bsys_desc *d, unsigned int nr);
void *sys_baddr(void);

/*
 * Kernel system call definitions
 */

/* FIXME: could use Sparse to statically check */
#define __user

extern long bsys_udp_send(void __user *addr, size_t len,
			  struct ip_tuple __user *id,
			  unsigned long cookie);
extern long bsys_udp_sendv(struct sg_entry __user *ents,
			   unsigned int nrents,
			   struct ip_tuple __user *id,
			   unsigned long cookie);
extern long bsys_udp_recv_done(void *iomap);

extern long bsys_tcp_connect(struct ip_tuple __user *id,
			     unsigned long cookie);
extern long bsys_tcp_accept(hid_t handle, unsigned long cookie);
extern long bsys_tcp_reject(hid_t handle);
extern ssize_t bsys_tcp_send(hid_t handle, void *addr, size_t len);
extern ssize_t bsys_tcp_sendv(hid_t handle, struct sg_entry __user *ents,
			      unsigned int nrents);
extern long bsys_tcp_recv_done(hid_t handle, size_t len);
extern long bsys_tcp_close(hid_t handle);

extern long bsys_nvme_open(long dev_id, long ns_id);
extern long bsys_nvme_close(long dev_id, long ns_id, hqu_t handle);
extern long bsys_nvme_register_flow(long flow_group_id, unsigned long cookie, 
				unsigned int latency_us_SLO, unsigned long IOPS_SLO, 
				int rw_ratio_SLO);
extern long bsys_nvme_unregister_flow(long flow_group_id); 
extern long bsys_nvme_write(hqu_t priority, void *buf, unsigned long lba,
			    unsigned int lba_count, unsigned long cookie);
extern long bsys_nvme_read(hqu_t priority, void * buf, unsigned long lba,
			   unsigned int lba_count, unsigned long cookie);
extern long bsys_nvme_writev(hqu_t fg_handle, void **sgls, int num_sgls,
			     unsigned long lba, unsigned int lba_count, unsigned long cookie);

extern long bsys_nvme_readv(hqu_t fg_handle, void **sgls, int num_sgls,
			    unsigned long lba, unsigned int lba_count, unsigned long cookie);
  
struct dune_tf;
extern void do_syscall(struct dune_tf *tf, uint64_t sysnr);

extern int syscall_init_cpu(void);
extern void syscall_exit_cpu(void);



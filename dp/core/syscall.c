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
 * syscall.c - system call support
 *
 * FIXME: We should dispatch native system calls directly from the syscall
 * entry assembly to improve performance, but that requires changes to
 * libdune.
 */

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_per_lcore.h>
#include <rte_malloc.h>
#include <ix/stddef.h>
#include <ix/cfg.h>
#include <ix/syscall.h>
#include <ix/errno.h>
#include <ix/timer.h>
#include <ix/ethdev.h>
#include <ix/cpu.h>
#include <ix/kstats.h>
#include <ix/log.h>
#include <ix/control_plane.h>
#include <ix/ethfg.h>
#include <ix/nvmedev.h>
#include <ix/utimer.h>


#define UARR_MIN_CAPACITY	8192

RTE_DEFINE_PER_LCORE(struct bsys_arr *, usys_arr);
RTE_DEFINE_PER_LCORE(void *, usys_iomap);
RTE_DEFINE_PER_LCORE(unsigned long, syscall_cookie);
RTE_DEFINE_PER_LCORE(unsigned long, idle_cycles);


static bsysfn_t bsys_tbl[] = {
	(bsysfn_t) bsys_udp_send,
	(bsysfn_t) bsys_udp_sendv,
	(bsysfn_t) bsys_udp_recv_done,
	(bsysfn_t) bsys_tcp_connect,
	(bsysfn_t) bsys_tcp_accept,
	(bsysfn_t) bsys_tcp_reject,
	(bsysfn_t) bsys_tcp_send,
	(bsysfn_t) bsys_tcp_sendv,
	(bsysfn_t) bsys_tcp_recv_done,
	(bsysfn_t) bsys_tcp_close,
	(bsysfn_t) bsys_nvme_write,
	(bsysfn_t) bsys_nvme_read,
	(bsysfn_t) bsys_nvme_writev,
	(bsysfn_t) bsys_nvme_readv,
	(bsysfn_t) bsys_nvme_open,
	(bsysfn_t) bsys_nvme_close,
	(bsysfn_t) bsys_nvme_register_flow,
	(bsysfn_t) bsys_nvme_unregister_flow
};

//
// TODO: Get rid of these eventually
//
// Empty UDP functions to allow for compilation
//
long bsys_udp_send(void __user *addr, size_t len,
			  struct ip_tuple __user *id,
			  unsigned long cookie)
{
	return 0;
}

long bsys_udp_sendv(struct sg_entry __user *ents,
			   unsigned int nrents,
			   struct ip_tuple __user *id,
			   unsigned long cookie)
{
	return -RET_NOSYS;
}

long bsys_udp_recv_done(void *iomap)
{
	return 0;
}


static int bsys_dispatch_one(struct bsys_desc __user *d)
{
	uint64_t sysnr, arga, argb, argc, argd, arge, argf, ret;

	sysnr = d->sysnr;
	arga = d->arga;
	argb = d->argb;
	argc = d->argc;
	argd = d->argd;
	arge = d->arge;
	argf = d->argf;
	
	if (unlikely(sysnr >= KSYS_NR)) {
		ret = (uint64_t) - ENOSYS;
		goto out;
	}

    //printf("DEBUGGG :Sysnr: %d\n", sysnr);
	ret = bsys_tbl[sysnr](arga, argb, argc, argd, arge, argf);

out:
	arga = percpu_get(syscall_cookie);
	d->arga = arga;
	d->argb = ret;

	return 0;
}

static int bsys_dispatch(struct bsys_desc __user *d, unsigned int nr)
{
	unsigned long i;
	int ret;
#ifdef ENABLE_KSTATS
	kstats_accumulate save;
#endif

	if (!nr)
		return 0;

	for (i = 0; i < nr; i++) {
		KSTATS_PUSH(bsys_dispatch_one, &save);
		ret = bsys_dispatch_one(&d[i]);
		KSTATS_POP(&save);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

/**
 * sys_bpoll - performs I/O processing and issues a batch of system calls
 * @d: the batched system call descriptor array
 * @nr: the number of batched system calls
 *
 * Returns 0 if successful, otherwise failure.
 */
int sys_bpoll(struct bsys_desc *d, unsigned int nr)
{
	int ret, empty;

	usys_reset();

	//KSTATS_PUSH(tx_reclaim, NULL);
	//eth_process_reclaim();
	//KSTATS_POP(NULL);

	KSTATS_PUSH(bsys, NULL);
	ret = bsys_dispatch(d, nr);
	KSTATS_POP(NULL);

	if (ret)
		return ret;

	percpu_get(received_nvme_completions) = 0;
again:
	switch (percpu_get(cp_cmd)->cmd_id) {
	case CP_CMD_MIGRATE:
		if (percpu_get(usys_arr)->len) {
			/* If there are pending events and we have
			 * received a migration command, return to user
			 * space for the processing of the events. We
			 * will delay the migration until we are in a
			 * quiescent state. */
			return 0;
		}
		//NOTE: not supporting migration now
		//eth_fg_assign_to_cpu((bitmap_ptr) percpu_get(cp_cmd)->migrate.fg_bitmap, percpu_get(cp_cmd)->migrate.cpu);
		//percpu_get(cp_cmd)->cmd_id = CP_CMD_NOP;
		break;
	case CP_CMD_IDLE:
		if (percpu_get(usys_arr)->len)
			return 0;
		cp_idle();
	case CP_CMD_NOP:
		break;
	}

	//schedule
	if (nvme_sched_flag) {
		nvme_sched();
	}
	

	KSTATS_PUSH(percpu_bookkeeping, NULL);
	cpu_do_bookkeeping();
	KSTATS_POP(NULL);

	KSTATS_PUSH(timer, NULL);
	timer_run();
	unset_current_fg();
	KSTATS_POP(NULL);

	KSTATS_PUSH(rx_poll, NULL);
    //printf("     DEBUGGG: CALLING ETH PROCESS POLL\n");
	eth_process_poll();
	KSTATS_POP(NULL);

	//KSTATS_PUSH(rx_recv, NULL);
	//empty = eth_process_recv();
	//KSTATS_POP(NULL);

	nvme_process_completions();

	KSTATS_PUSH(tx_send, NULL);
    //printf("     DEBUGGG: CALLING ETH PROCESS SEND\n");
	eth_process_send();
	KSTATS_POP(NULL);
	return 0;
}

/**
 * sys_bcall - issues a batch of system calls
 * @d: the batched system call descriptor array
 * @nr: the number of batched system calls
 *
 * Returns 0 if successful, otherwise failure.
 */
static int sys_bcall(struct bsys_desc __user *d, unsigned int nr)
{
	int ret;

	KSTATS_PUSH(tx_reclaim, NULL);
	eth_process_reclaim();
	KSTATS_POP(NULL);

	KSTATS_PUSH(bsys, NULL);
	ret = bsys_dispatch(d, nr);
	KSTATS_POP(NULL);

	KSTATS_PUSH(tx_send, NULL);
	eth_process_send();
	KSTATS_POP(NULL);

	return ret;
}

/**
 * sys_baddr - get the address of the batched syscall array
 *
 * Returns an IOMAP pointer.
 */
void *sys_baddr(void)
{
	//return percpu_get(usys_iomap);
	return percpu_get(usys_arr);
}

/**
 * sys_mmap - maps pages of memory into userspace
 * @addr: the start address of the mapping
 * @nr: the number of pages
 * @size: the size of each page
 * @perm: the permission of the mapping
 *
 * Returns 0 if successful, otherwise fail.
 */
static int sys_mmap(void *addr, int nr, int size, int perm)
{
	log_err("sys_mmap not supported\n");
}

/**
 * sys_unmap - unmaps pages of memory from userspace
 * @addr: the start address of the mapping
 * @nr: the number of pages
 * @size: the size of each page
 */
static int sys_unmap(void *addr, int nr, int size)
{
	log_err("sys_unmap not supported\n");
}

bool sys_spawn_cores;

/**
 * sys_spawnmode - sets the spawn mode
 * @spawn_cores: the spawn mode. If true, calls to clone will bind
 * to IX cores. If false, calls to clone will spawn a regular linux
 * thread.
 *
 * Returns 0.
 */
static int sys_spawnmode(bool spawn_cores)
{
	sys_spawn_cores = spawn_cores;
	return 0;
}

/**
 * sys_nrcpus - returns the number of active CPUs
 */
static int sys_nrcpus(void)
{
	return cpus_active;
}

/**
 * sys_timer_init - initializes a timer
 *
 * Returns the timer id
 */
static int sys_timer_init(void *addr)
{
	return utimer_init(&percpu_get(utimers), addr);
}

/**
 * sys_timer_ctl - arm the timer
 *
 * Returns 0 or failure
 */
static int sys_timer_ctl(int timer_id, uint64_t delay)
{
	return utimer_arm(&percpu_get(utimers), timer_id, delay);
}

typedef uint64_t (*sysfn_t)(uint64_t, uint64_t, uint64_t,
			    uint64_t, uint64_t, uint64_t, uint64_t);

static sysfn_t sys_tbl[] = {
	(sysfn_t) sys_bpoll,
	(sysfn_t) sys_bcall,
	(sysfn_t) sys_baddr,
	(sysfn_t) sys_mmap, 		//FIXME: don't need
	(sysfn_t) sys_unmap,		//FIXME: don't need
	(sysfn_t) sys_spawnmode,
	(sysfn_t) sys_nrcpus,
	(sysfn_t) sys_timer_init,
	(sysfn_t) sys_timer_ctl,
};

/**
 * do_syscall - the main IX system call handler routine
 * @tf: the Dune trap frame
 * @sysnr: the system call number
 */
/*
void do_syscall(struct dune_tf *tf, uint64_t sysnr)
{
	if (unlikely(sysnr >= SYS_NR)) {
		tf->rax = (uint64_t) - ENOSYS;
		return;
	}

	KSTATS_POP(NULL);
	tf->rax = (uint64_t) sys_tbl[sysnr](tf->rdi, tf->rsi, tf->rdx,
					    tf->rcx, tf->r8, tf->r9, tf->r10);
	KSTATS_PUSH(user, NULL);
}
*/

/**
 * syscall_init_cpu - creates a user-mapped page for batched system calls
 *
 * Returns 0 if successful, otherwise fail.
 */
int syscall_init_cpu(void)
{
	struct bsys_arr *arr;
	void *iomap;
	arr = (struct bsys_arr *) rte_malloc(NULL, sizeof(struct bsys_arr) + UARR_MIN_CAPACITY * sizeof(struct bsys_desc), 0);
	if (!arr)
	{
		return -ENOMEM;
	}
	
	/*
	iomap = vm_map_to_user((void *) arr, usys_nr, PGSIZE_2MB, VM_PERM_R);
	if (!iomap) {
		page_free_contig((void *) arr, usys_nr);
		return -ENOMEM;
	}
	*/

	percpu_get(usys_arr) = arr;
	log_info("syscall_init_cpu: usys_arr pts is %p @@@@@@@@@@@@@@@@@@\n", arr);
	return 0;
}

/**
 * syscall_exit_cpu - frees the user-mapped page for batched system calls
 */
void syscall_exit_cpu(void)
{
	rte_free(percpu_get(usys_arr));
	percpu_get(usys_arr) = NULL;
	percpu_get(usys_iomap) = NULL;
}


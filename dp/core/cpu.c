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
 * cpu.c - support for multicore and percpu data.
 */

#define _GNU_SOURCE

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_malloc.h>

#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>

#include <ix/stddef.h>
#include <ix/errno.h>
#include <ix/log.h>

#include <ix/lock.h>
#include <ix/cpu.h>

int cpu_count;
int cpus_active;

RTE_DEFINE_PER_LCORE(unsigned int, cpu_numa_node);
RTE_DEFINE_PER_LCORE(unsigned int, cpu_id);
RTE_DEFINE_PER_LCORE(unsigned int, cpu_nr);

void *percpu_offsets[NCPU];

extern const char __percpu_start[];
extern const char __percpu_end[];

#define PERCPU_DUNE_LEN	512

struct cpu_runner {
	struct cpu_runner *next;
	cpu_func_t func;
	void *data;
};

struct cpu_runlist {
	spinlock_t lock;
	struct cpu_runner *next_runner;
} __aligned(CACHE_LINE_SIZE);

#define MAX_LCORES 128
static struct cpu_runlist global_runlists[MAX_LCORES];

/**
 * cpu_run_on_one - calls a function on the specified CPU
 * @func: the function to call
 * @data: an argument for the function
 * @cpu: the CPU to run on
 *
 * Returns 0 if successful, otherwise fail.
 */
int cpu_run_on_one(cpu_func_t func, void *data, unsigned int cpu)
{
	struct cpu_runner *runner;
	struct cpu_runlist *rlist;

	if (cpu >= cpu_count)
		return -EINVAL;

	runner = malloc(sizeof(*runner));
	if (!runner)
		return -ENOMEM;

	runner->func = func;
	runner->data = data;
	runner->next = NULL;

	rlist = &global_runlists[cpu];

	spin_lock(&rlist->lock);
	runner->next = rlist->next_runner;
	rlist->next_runner = runner;
	spin_unlock(&rlist->lock);

	return 0;
}

/**
 * cpu_do_bookkepping - runs periodic per-cpu tasks
 */
void cpu_do_bookkeeping(void)
{
	struct cpu_runlist *rlist = &global_runlists[percpu_get(cpu_id)];
	struct cpu_runner *runner;

	if (rlist->next_runner) {
		spin_lock(&rlist->lock);
		runner = rlist->next_runner;
		rlist->next_runner = NULL;
		spin_unlock(&rlist->lock);

		do {
			struct cpu_runner *last = runner;
			runner->func(runner->data);
			runner = runner->next;
			free(last);
		} while (runner);
	}
}


/**
 * cpu_init_one - initializes a CPU core
 * @cpu: the CPU core number
 *
 * Typically one should call this right after
 * creating a new thread. Initialization includes
 * binding the thread to the appropriate core,
 * setting up per-cpu memory, and enabling Dune.
 *
 * Returns 0 if successful, otherwise fail.
 */
int cpu_init_one(unsigned int cpu)
{
	int ret;
	cpu_set_t mask;
	unsigned int tmp, numa_node;
	void *pcpu;

	if (cpu >= cpu_count)
		return -EINVAL;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	ret = sched_setaffinity(0, sizeof(mask), &mask);
	if (ret)
		return -EPERM;

	ret = syscall(SYS_getcpu, &tmp, &numa_node, NULL);
	if (ret)
		return -ENOSYS;

	if (cpu != tmp) {
		log_err("cpu: couldn't migrate to the correct core\n");
		return -EINVAL;
	}
/*
	pcpu = cpu_init_percpu(cpu, numa_node);

	if (!pcpu)
		return -ENOMEM;
*/

	RTE_PER_LCORE(cpu_id) = cpu;

	RTE_PER_LCORE(cpu_numa_node) = numa_node;
	log_is_early_boot = false;

	log_info("cpu: started core %d, numa node %d\n", cpu, numa_node);

	return 0;
}

/**
 * cpu_init - initializes CPU support
 *
 * Returns zero if successful, otherwise fail.
 */
int cpu_init(void)
{
	cpu_count = sysconf(_SC_NPROCESSORS_CONF);

	if (cpu_count <= 0 || cpu_count > NCPU)
		return -EINVAL;

	log_info("cpu: detected %d cores\n", cpu_count);

	return 0;
}


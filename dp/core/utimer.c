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

#include <rte_per_lcore.h>
#include <ix/syscall.h>
#include <ix/timer.h>

/* max number of supported user level timers */
#define UTIMER_COUNT 32

struct utimer {
	struct timer t;
	void *cookie;
};

struct utimer_list {
	struct utimer arr[UTIMER_COUNT];
};

RTE_DEFINE_PER_LCORE(struct utimer_list, utimers);

void generic_handler(struct timer *t, struct eth_fg *unused)
{
	struct utimer *ut;
	ut = container_of(t, struct utimer, t);
	usys_timer((unsigned long) ut->cookie);
}

static int find_available(struct utimer_list *tl)
{
	static int next;

	if (next >= UTIMER_COUNT)
		return -1;

	return next++;
}

int utimer_init(struct utimer_list *tl, void *udata)
{
	struct utimer *ut;
	int index;

	index = find_available(tl);
	if (index < 0)
		return -1;

	ut = &tl->arr[index];
	ut->cookie = udata;
	timer_init_entry(&ut->t, generic_handler);

	return index;
}

int utimer_arm(struct utimer_list *tl, int timer_id, uint64_t delay)
{
	struct timer *t;
	t = &tl->arr[timer_id].t;
	return timer_add(t, NULL, delay);
}

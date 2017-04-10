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
 * lock.h - locking primitives
 */

#pragma once

#include <asm/cpu.h>
#include <ix/types.h>
#include <assert.h>
#include <ix/log.h>

#define SPINLOCK_INITIALIZER_RECURSIVE {.locked = 0}
#define DEFINE_SPINLOCK_RECURSIVE(name) \
	spinlock_t name = SPINLOCK_INITIALIZER_RECURSIVE
#define DECLARE_SPINLOCK_RECURSIVE(name) \
	extern spinlock_rec_t name

typedef struct {
	volatile unsigned int locked;
	volatile unsigned int recursive_count;
} spinlock_rec_t;


/**
 * spin_lock_recursive_init - prepares a recursive spin lock for use
 * @l: the recursive spin lock
 */
static inline void 
spin_lock_recursive_init(spinlock_rec_t *l)
{
	l->locked = 0;
	l->recursive_count = 0;
}


/**
 * spin_set_recursive - makes this lock a recursive lock
 * @l: the spin lock
 */
static inline int
spin_set_recursive(spinlock_rec_t *l)
{
	return 0;
}

static inline void
spin_lock_recursive(spinlock_rec_t *l) 
{
	unsigned long cid = percpu_get(cpu_id);

	if(l->locked == (long)cid) //we own the lock
		l->recursive_count++;
	else{
		while(!__sync_bool_compare_and_swap(&l->locked, 0x0UL, cid)) {
			while(l->locked) {
				cpu_relax();
			}
		}		
		l->recursive_count++;
	}
}

static inline void
spin_unlock_recursive(spinlock_rec_t *l)
{
	unsigned long cid = percpu_get(cpu_id);

	if (l->locked != cid)
		assert(l->locked == cid);

	l->recursive_count--;
	if (l->recursive_count == 0) 
		__sync_lock_release(&l->locked);
}

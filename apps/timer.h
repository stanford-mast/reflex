/*
 * timer.h - timer event infrastructure
 */

#pragma once

#include <time.h>
#include <ix/list.h>

struct eth_fg;

struct timer {
	struct hlist_node link;
	void (*handler)(struct timer *t, struct eth_fg *cur_fg);
	uint64_t expires;
	int fg_id;
};


#define ONE_SECOND	1000000
#define ONE_MS		1000
#define ONE_US		1
/**
 * timer_init_entry - initializes a timer
 * @t: the timer
 */
static inline void
timer_init_entry(struct timer *t, void (*handler)(struct timer *t, struct eth_fg *))
{
	t->link.prev = NULL;
	t->handler = handler;
}

/**
 * timer_pending - determines if a timer is pending
 * @t: the timer
 *
 * Returns true if the timer is pending, otherwise false.
 */
static inline bool timer_pending(struct timer *t)
{
	return t->link.prev != NULL;
}

extern int timer_add(struct timer *t, struct eth_fg *,uint64_t usecs);
extern void timer_add_for_next_tick(struct timer *t,struct eth_fg *);
extern void timer_add_abs(struct timer *t,struct eth_fg *,uint64_t usecs);
extern uint64_t timer_now(void);

static inline void __timer_del(struct timer *t)
{
	hlist_del(&t->link);
	t->link.prev = NULL;
}

/**
 * timer_mod - modifies a timer
 * @t: the timer
 * @usecs: the number of microseconds from present to fire the timer
 *
 * If the timer is already armed, then its trigger time is modified.
 * Otherwise this function behaves like timer_add().
 *
 * Returns 0 if successful, otherwise failure.
 */
static inline int timer_mod(struct timer *t, struct eth_fg *cur_fg,uint64_t usecs)
{
	if (timer_pending(t))
		__timer_del(t);
	return timer_add(t, cur_fg,usecs);
}

/**
 * timer_del - disarms a timer
 * @t: the timer
 *
 * If the timer is already disarmed, then nothing happens.
 */
static inline void timer_del(struct timer *t)
{
	if (timer_pending(t))
		__timer_del(t);
}

extern void timer_run(void);
extern uint64_t timer_deadline(uint64_t max_us);

extern int timer_collect_fgs(uint8_t *fg_vector, struct hlist_head *list,uint64_t *timer_pos);
extern void timer_reinject_fgs(struct hlist_head *list,uint64_t timer_pos);


extern void timer_init_fg(void);
extern int timer_init_cpu(void);
extern int timer_init(void);

//extern int cycles_per_us;


int cycles_per_us;

int timer_calibrate_tsc(void)
{
        struct timespec sleeptime = {.tv_nsec = 5E8 }; /* 1/2 second */
        struct timespec t_start, t_end;

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
                uint64_t ns, end, start;
                double secs;

                start = rdtsc();
                nanosleep(&sleeptime,NULL);
                clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
                end = rdtsc();
                ns = ((t_end.tv_sec - t_start.tv_sec) * 1E9);
                ns += (t_end.tv_nsec - t_start.tv_nsec);

                secs = (double)ns / 1000;
                cycles_per_us = (uint64_t)((end - start)/secs);
                return 0;
        }

        return -1;
}

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
 * ethqueue.c - ethernet queue support
 */

#include <rte_per_lcore.h>
#include <ix/stddef.h>
#include <ix/kstats.h>
#include <ix/ethdev.h>
#include <ix/log.h>
#include <ix/control_plane.h>

#include <ix/byteorder.h>

/* Accumulate metrics period (in us) */
#define METRICS_PERIOD_US 10000

/* Power measurement period (in us) */
#define POWER_PERIOD_US 500000

#define MAX_PKT_BURST 64

#define EMA_SMOOTH_FACTOR_0 0.5
#define EMA_SMOOTH_FACTOR_1 0.25
#define EMA_SMOOTH_FACTOR_2 0.125
#define EMA_SMOOTH_FACTOR EMA_SMOOTH_FACTOR_0
#define MAX_NUM_IO_QUEUES 31

RTE_DEFINE_PER_LCORE(int, eth_num_queues);
RTE_DEFINE_PER_LCORE(struct eth_rx_queue *, eth_rxqs[NETHDEV]);
RTE_DEFINE_PER_LCORE(struct eth_tx_queue *, eth_txqs[NETHDEV]);

struct metrics_accumulator {
	long timestamp;
	long queuing_delay;
	int batch_size;
	int count;
	long queue_size;
	long loop_duration;
	long prv_timestamp;
};

static RTE_DEFINE_PER_LCORE(struct metrics_accumulator, metrics_acc);

struct power_accumulator {
	int prv_energy;
	long prv_timestamp;
};

static struct power_accumulator power_acc;

unsigned int eth_rx_max_batch = 64;

//#define PRINT_RTE_STATS 1

#ifdef PRINT_RTE_STATS
int count_stats = 0;
#endif
/**
 * eth_process_poll - polls HW for new packets
 *
 * Returns the number of new packets received.
 */
int eth_process_poll(void) 
{
	int i, ret = 0;
	int count = 0; 
	bool empty;
	struct rte_mbuf *m;

	struct rte_mbuf *rx_pkts[MAX_NUM_IO_QUEUES]; //TODO: test with multi queue, multicore; assumes 1 pkt recv at a time
	
	/*
	 * We round robin through each queue one packet at
	 * a time for fairness, and stop when all queues are
	 * empty or the batch limit is hit. We're okay with
	 * going a little over the batch limit if it means
	 * we're not favoring one queue over another.
	 */
	do {
		empty = true;

		// This loops over each queue of a single core (current behaviour is 1 queue to 1 core)
		// Note that i is the index within a core's list of queues, not the global list of queues.
		for (i = 0; i < percpu_get(eth_num_queues); i++) {
			//burst 1 because check queues round-robin
			//  Note: Using percpu_get(cpu_id) requires one queue to one core and identical cpu and queue numbering.
			ret = rte_eth_rx_burst(active_eth_port, percpu_get(cpu_id), &rx_pkts[i], 1);
			if (ret) {
				empty = false;
				m = rx_pkts[i];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *)); 
				eth_input_process(rx_pkts[i], ret);
			
#ifdef PRINT_RTE_STATS	
				struct rte_eth_stats stats;	
				rte_eth_stats_get(0, &stats);
				count_stats++;
				if (count_stats % 1000000) {
					printf("Stats: NIC drop or error:  %f out of %f \n",
							(double) stats.imissed + stats.ierrors + stats.oerrors, (double) (stats.imissed + stats.ipackets));
					count_stats = 0;
				}
#endif

			}
			count += ret;
		}
	} while (!empty && count < eth_rx_max_batch);

	return count;
}

static int eth_process_recv_queue(struct eth_rx_queue *rxq)
{
	struct mbuf *pos = rxq->head;
#ifdef ENABLE_KSTATS
	kstats_accumulate tmp;
#endif

	if (!pos)
		return -EAGAIN;

	/* NOTE: pos could get freed after eth_input(), so check next here */
	rxq->head = pos->next;
	rxq->len--;

	KSTATS_PUSH(eth_input, &tmp);
	eth_input(rxq, pos);
	KSTATS_POP(&tmp);

	return 0;
}

/**
 * eth_process_recv - processes pending received packets
 *
 * Returns true if there are no remaining packets.
 */
int eth_process_recv(void)
{
	int i, count = 0;
	bool empty;
	unsigned long min_timestamp = -1;
	unsigned long timestamp;
	int value;
	struct metrics_accumulator *this_metrics_acc = &percpu_get(metrics_acc);
	int backlog;
	double idle;
	unsigned int energy;
	int energy_diff;

	/*
	 * We round robin through each queue one packet at
	 * a time for fairness, and stop when all queues are
	 * empty or the batch limit is hit. We're okay with
	 * going a little over the batch limit if it means
	 * we're not favoring one queue over another.
	 */
	do {
		empty = true;
		for (i = 0; i < percpu_get(eth_num_queues); i++) {
			struct eth_rx_queue *rxq = percpu_get(eth_rxqs[i]);
			struct mbuf *pos = rxq->head;
			if (pos)
				min_timestamp = min(min_timestamp, pos->timestamp);
			if (!eth_process_recv_queue(rxq)) {
				count++;
				empty = false;
			}
		}
	} while (!empty && count < eth_rx_max_batch);

	backlog = 0;
	for (i = 0; i < percpu_get(eth_num_queues); i++)
		backlog += percpu_get(eth_rxqs[i])->len;

	timestamp = rdtsc();
	this_metrics_acc->count++;
	value = count ? (timestamp - min_timestamp) / cycles_per_us : 0;
	this_metrics_acc->queuing_delay += value;
	this_metrics_acc->batch_size += count;
	this_metrics_acc->queue_size += count + backlog;
	this_metrics_acc->loop_duration += timestamp - this_metrics_acc->prv_timestamp;
	this_metrics_acc->prv_timestamp = timestamp;
	if (timestamp - this_metrics_acc->timestamp > (long) cycles_per_us * METRICS_PERIOD_US) {
		idle = (double) percpu_get(idle_cycles) / (timestamp - this_metrics_acc->timestamp);
		EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].idle[0], idle, EMA_SMOOTH_FACTOR_0);
		EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].idle[1], idle, EMA_SMOOTH_FACTOR_1);
		EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].idle[2], idle, EMA_SMOOTH_FACTOR_2);
		if (this_metrics_acc->count) {
			this_metrics_acc->loop_duration -= percpu_get(idle_cycles);
			this_metrics_acc->loop_duration /= cycles_per_us;
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queuing_delay, (double) this_metrics_acc->queuing_delay / this_metrics_acc->count, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].batch_size, (double) this_metrics_acc->batch_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[0], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_0);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[1], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_1);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[2], (double) this_metrics_acc->queue_size / this_metrics_acc->count, EMA_SMOOTH_FACTOR_2);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].loop_duration, (double) this_metrics_acc->loop_duration / this_metrics_acc->count, EMA_SMOOTH_FACTOR_0);
		} else {
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queuing_delay, 0, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].batch_size, 0, EMA_SMOOTH_FACTOR);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[0], 0, EMA_SMOOTH_FACTOR_0);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[1], 0, EMA_SMOOTH_FACTOR_1);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].queue_size[2], 0, EMA_SMOOTH_FACTOR_2);
			EMA_UPDATE(cp_shmem->cpu_metrics[RTE_PER_LCORE(cpu_nr)].loop_duration, 0, EMA_SMOOTH_FACTOR_0);
		}
		this_metrics_acc->timestamp = timestamp;
		percpu_get(idle_cycles) = 0;
		this_metrics_acc->count = 0;
		this_metrics_acc->queuing_delay = 0;
		this_metrics_acc->batch_size = 0;
		this_metrics_acc->queue_size = 0;
		this_metrics_acc->loop_duration = 0;
	}
	// NOTE: assuming that the first CPU never idles
	/* TODO: NOT SUPPORTING ENERGY CHANGES RIGHT NOW
	if (percpu_get(cpu_nr) == 0 && timestamp - power_acc.prv_timestamp > (long) cycles_per_us * POWER_PERIOD_US) {
		energy = rdmsr(MSR_PKG_ENERGY_STATUS);
		if (power_acc.prv_timestamp) {
			energy_diff = energy - power_acc.prv_energy;
			if (energy_diff < 0)
				energy_diff += 1 << 31;
			cp_shmem->pkg_power = (double) energy_diff * energy_unit / (timestamp - power_acc.prv_timestamp) * cycles_per_us * 1000000;
		} else {
			cp_shmem->pkg_power = 0;
		}
		power_acc.prv_timestamp = timestamp;
		power_acc.prv_energy = energy;
	}
	*/

	KSTATS_PACKETS_INC(count);
	KSTATS_BATCH_INC(count);
#ifdef ENABLE_KSTATS
	backlog = div_up(backlog, eth_rx_max_batch);
	KSTATS_BACKLOG_INC(backlog);
#endif

	return empty;
}


RTE_DEFINE_PER_LCORE(struct rte_eth_dev_tx_buffer*, tx_buf);
/**
 * ethdev_init_cpu - initializes the core-local tx buffer 
 *
 * Returns 0 if successful, otherwise failure.
 */

int ethdev_init_cpu(void)
{
	int ret;
	struct rte_eth_dev_tx_buffer* tx_buffer = percpu_get(tx_buf);

	tx_buffer = rte_zmalloc_socket("tx_buffer",
			RTE_ETH_TX_BUFFER_SIZE(eth_rx_max_batch), 0,
			rte_eth_dev_socket_id(active_eth_port));
	if (tx_buffer == NULL){
		log_err("ERROR: cannot allocate buffer for tx \n");
		exit(0);
	}

	ret = rte_eth_tx_buffer_init(tx_buffer, eth_rx_max_batch);
	if (ret){
		log_err("ERROR in tx buffer init\n");
		exit(0);
	}
	percpu_get(tx_buf) = tx_buffer;

	// Assign each CPU the correct number of queues.
	percpu_get(eth_num_queues) = rte_eth_dev_count();

	return 0;
}



/**
 * eth_process_send - processes packets pending to be sent
 */
void eth_process_send(void)
{
	int i, nr;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		// NOTE: rte_eth_tx_buffer_flush appears to flush all queues regardless of the parameter given.
		// Currently incompatible with multiple queues per CPU core due to cpu_id being queue number.
		nr = rte_eth_tx_buffer_flush(active_eth_port, percpu_get(cpu_id), percpu_get(tx_buf)); 
		//if (nr) printf("CE_DEBUG: rte_eth_tx_buffer_flush() nr: %d \n", nr);
	}


}

/**
 * eth_process_reclaim - processs packets that have completed sending
 */
void eth_process_reclaim(void)
{
	int i;
	struct eth_tx_queue *txq;

	for (i = 0; i < percpu_get(eth_num_queues); i++) {
		txq = percpu_get(eth_txqs[i]);
		txq->cap = eth_tx_reclaim(txq);
	}
}


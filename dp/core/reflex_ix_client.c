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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include <ixev.h>
#include <ix/mempool.h>
#include <ix/list.h>
#include <ix/timer.h>

#include "reflex.h" 

#include <netinet/in.h>

#define BINARY_HEADER binary_header_blk_t

#define ROUND_UP(num, multiple) ((((num) + (multiple) - 1) / (multiple)) * (multiple))

#define MAKE_IP_ADDR(a, b, c, d)			\
	(((uint32_t) a << 24) | ((uint32_t) b << 16) |	\
	 ((uint32_t) c << 8) | (uint32_t) d)


#define MAX_SECTORS_PER_ACCESS 64
#define MAX_LATENCY 5000 //2000
#define MAX_IOPS 900000
#define NUM_TESTS 5 //16
#define DURATION 1
#define MAX_NUM_MEASURE MAX_IOPS * DURATION
static const unsigned long sweep[NUM_TESTS] = {1000, 1100, 1200, 1300, 1400};
/*
static const unsigned long sweep[NUM_TESTS] = {1000, 10000, 100000,
					       150000, 200000, 250000, 300000,
					       400000, 500000, 600000, 650000, 700000, 750000,
					       800000, 850000,  MAX_IOPS};

static const unsigned long sweep[NUM_TESTS] = {1000, 10000, 25000, 30000, //100000,
					       35000, 40000, 50000, 60000,
					       70000, 80000, 90000, 100000,
					       200000, 250000, 900000, MAX_IOPS};
*/

//fixme: hard-coding sector size for now
static int ns_sector_size = 512;
//fixme: hard-coding namespace size for device tested
static long ns_size = 0x1749a956000;    // Samsung 1725 
// static long ns_size = 0x5d27216000;  // Intel P3600 400GB capacity 


static struct mempool_datastore nvme_usr_datastore;
static const int outstanding_reqs = 512; // 4096 * 8;
static volatile int started_conns = 0;
static pthread_barrier_t barrier;
static int req_size = 1024;
static volatile int nr_threads = 1;
static unsigned long iops = 0;
static int sequential = 0;
struct ip_tuple *ip_tuple[64];
static int read_percentage = 100;
static int SWEEP = 1;
static bool preconditioning = 0;
static unsigned long global_target_IOPS = 50000;
static int qdepth = 0;
static int run_time = 0;
time_t start_time;
time_t curr_time;
static int port = -1;
static char* ip = NULL;

static __thread struct mempool req_pool;
static __thread int conn_opened;
static __thread unsigned long last_send = 0;
static __thread unsigned long bench_start = 0;
static __thread unsigned long avg = 0;
static __thread unsigned long max = 0;
static __thread unsigned long measure = 0;
static __thread unsigned long num_measured_reads = 0;
static __thread long sent = 0;
static __thread unsigned long measurements[MAX_LATENCY];
static __thread unsigned long missed_sends = 0;
static __thread bool running = false;
static __thread bool terminate = false;
static __thread int run = 0;
static __thread int tid;
static __thread long cycles_between_req;
static __thread unsigned long phase_start;
static __thread long NUM_MEASURE;

static struct mempool_datastore nvme_req_buf_datastore;
static __thread struct mempool nvme_req_buf_pool;

static inline uint32_t intlog2(const uint32_t x) {
	uint32_t y;
	asm ( "\tbsr %1, %0\n"
	      : "=r"(y)
	      : "r" (x)
		);
	return y;
}

int
compare_ul (const void *a, const void *b)
{
	const unsigned long *da = (const unsigned long *) a;
	const unsigned long *db = (const unsigned long *) b;

	return (*da > *db) - (*da < *db);
}

long get_percentile(unsigned long m[], unsigned long num_measure, unsigned long percentile) {
 	unsigned int i = 0;
 	unsigned long tmp = 0;
 
 	while(tmp < (num_measure * percentile) / 100 && i < MAX_LATENCY) {
 		tmp += m[i];
 		i++;
 	}
 	return i;
}

struct nvme_req {
	uint8_t cmd;
	unsigned long lba;
	unsigned int lba_count;
	struct ixev_nvme_req_ctx ctx;
	size_t size;
	struct pp_conn *conn;
	struct ixev_ref ref; 		//for zero-copy
	struct list_node link;
	unsigned long sent_time;
	void *remote_req_handle;
	char *buf;					//nvme buffer to read/write data into
};


struct pp_conn {
	struct ixev_ctx ctx;
	size_t rx_received;			//the amount of data received/sent for the current ReFlex request
	size_t tx_sent;
	bool rx_pending;			//is there a ReFlex req currently being received/sent
	bool tx_pending;
	int nvme_pending;
	long in_flight_pkts;
	long sent_pkts;
	long list_len;
	bool receive_loop;
	unsigned long seq_count;
	struct list_head pending_requests;
	long nvme_fg_handle; 		//nvme flow group handle
	char data[4096 + sizeof(BINARY_HEADER)];
	char data_send[sizeof(BINARY_HEADER)];
};

static struct mempool_datastore pp_conn_datastore;
static __thread struct mempool pp_conn_pool;

static int parse_ip_addr(const char *str, uint32_t *addr)
{
	unsigned char a, b, c, d;

	if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
		return -EINVAL;

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}


static void receive_req(struct pp_conn *conn)
{
	ssize_t ret;
	struct nvme_req *req;
	BINARY_HEADER* header;
	int measure_cond, report_cond, terminate_cond; 
	
	while(1) {
		if(!conn->rx_pending) {
			ret = ixev_recv(&conn->ctx, &conn->data[conn->rx_received],
					sizeof(BINARY_HEADER) - conn->rx_received); 
			if (ret <= 0) {
				if (ret != -EAGAIN) {
					if(!conn->nvme_pending) {
						printf("Connection close 6\n");
						ixev_close(&conn->ctx);
					}
				}
				break;
			}
			else
				conn->rx_received += ret;
			if(conn->rx_received < sizeof(BINARY_HEADER))
				return;
		}


		//received the header
		conn->rx_pending = true;
		header = (BINARY_HEADER *)&conn->data[0];
		
		assert(header->magic == sizeof(BINARY_HEADER)); 
				
		if (header->opcode == CMD_GET) {
			ret = ixev_recv(&conn->ctx,
					&conn->data[conn->rx_received],
					sizeof(BINARY_HEADER) +
					header->lba_count * ns_sector_size
					- conn->rx_received);
			if (ret <= 0) {
				if (ret != -EAGAIN) {
					assert(0);
					if(!conn->nvme_pending) {
						printf("Connection close 7\n");
						ixev_close(&conn->ctx);
					}
				}
				break;
			}
			conn->rx_received += ret;
			
			if(conn->rx_received < (sizeof(BINARY_HEADER) +
						header->lba_count *
						ns_sector_size))
				return;
		}
		else if (header->opcode == CMD_SET) {}
		else {
			printf("Received unsupported command, closing connection\n");
			ixev_close(&conn->ctx);
			return;
		}

		req = header->req_handle;

		if (!SWEEP && qdepth) {
			measure_cond = 1;
		} else {
			measure_cond = measure >= NUM_MEASURE && measure < NUM_MEASURE * 2;
		}
		//if (req->cmd == CMD_GET) { //only report read latency (not write)
			if (measure_cond) {
				unsigned long now = rdtsc();
				if(((now - req->sent_time) / cycles_per_us) >= MAX_LATENCY)
					measurements[MAX_LATENCY - 1]++;
				else
					measurements[(now - req->sent_time) / cycles_per_us]++;

				avg += (now - req->sent_time) / cycles_per_us;
				if (((now - req->sent_time) / cycles_per_us) > max)
					max = (now - req->sent_time) / cycles_per_us;
			
				num_measured_reads++;
			}
		//}
		measure++;
		req->sent_time = 0;
	
		mempool_free(&nvme_req_buf_pool, req->buf);

		mempool_free(&req_pool, req);
		conn->rx_pending = false;
		conn->rx_received = 0;	



		if (!SWEEP && qdepth) {
			time(&curr_time);
			report_cond = difftime(curr_time, start_time) > run_time && tid == 0 && num_measured_reads != 0;
			terminate_cond = difftime(curr_time, start_time) > run_time;
		} else {
			report_cond = measure == NUM_MEASURE * 2 && tid == 0 && num_measured_reads != 0;
			terminate_cond = measure == NUM_MEASURE * 3;
		}	



		if (report_cond) {
			unsigned long usecs = 1000UL * 1000UL;
			unsigned long target_IOPS;

			if (!qdepth) {		
				assert(measure <= MAX_NUM_MEASURE + NUM_MEASURE);
				assert(num_measured_reads <= NUM_MEASURE);
		        }
			
			if (SWEEP){
				target_IOPS = sweep[run];
			}
			else{
				target_IOPS = global_target_IOPS; 
			}

			printf("%lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\t %lu\n",
			       target_IOPS,
			       nr_threads * (NUM_MEASURE * usecs) / ((rdtsc() - phase_start) / cycles_per_us),
			       avg/num_measured_reads, 
			       get_percentile(measurements, num_measured_reads, 10),
			       get_percentile(measurements, num_measured_reads, 20),
			       get_percentile(measurements, num_measured_reads, 30),
			       get_percentile(measurements, num_measured_reads, 40),
			       get_percentile(measurements, num_measured_reads, 50),
			       get_percentile(measurements, num_measured_reads, 60),
			       get_percentile(measurements, num_measured_reads, 70),
			       get_percentile(measurements, num_measured_reads, 80),
			       get_percentile(measurements, num_measured_reads, 90),
			       get_percentile(measurements, num_measured_reads, 95),
			       get_percentile(measurements, num_measured_reads, 99),
			       max, missed_sends);
			run++;
		}

		if (terminate_cond) {
			printf("debug: terminating\n");
			if (!qdepth) assert(sent == NUM_MEASURE * 3);
			terminate = true;
			measure = 0;
			num_measured_reads = 0;
			avg = 0;
			max = 0;
			sent = 0;
			missed_sends = 0;
			for(int i = 0; i< MAX_LATENCY; i++){
				measurements[i] = 0;
			}
		}

		if (qdepth) {
			//close loop: send another request
			send_handler(&conn->ctx, 1);
		}

	}
}

/*
 * returns 0 if send was successfull and -1 if tx path is busy
 */
int send_client_req(struct nvme_req *req) 
{
	struct pp_conn *conn = req->conn;
	int ret = 0;
	BINARY_HEADER *header;
	
	if(!conn->tx_pending){
		//setup header
		header = (BINARY_HEADER *)&conn->data_send[0];
		header->magic = sizeof(BINARY_HEADER); 
		header->opcode = req->cmd;
		header->lba = req->lba;
		header->lba_count = req->lba_count;
		header->req_handle = req;

		while (conn->tx_sent < sizeof(BINARY_HEADER)) {
			ret = ixev_send(&conn->ctx, &conn->data_send[conn->tx_sent],
					sizeof(BINARY_HEADER) - conn->tx_sent);
			if (ret == -EAGAIN){
				return -1;
			}
			if (ret < 0) {
				if(!conn->nvme_pending) {
					printf("Connection close 2\n");
					ixev_close(&conn->ctx);
				}
				return -2;
				ret = 0;
			}
			conn->tx_sent += ret;
		}
	
		assert(conn->tx_sent==sizeof(BINARY_HEADER));
		conn->tx_pending = true;
		conn->tx_sent = 0;
	}
	ret = 0;
	if (req->cmd == CMD_SET) {
		while (conn->tx_sent < req->lba_count * ns_sector_size) {
			assert(req->lba_count * ns_sector_size);
			ret = ixev_send_zc(&conn->ctx, &req->buf[conn->tx_sent],
					   req->lba_count * ns_sector_size - conn->tx_sent); 
			if (ret < 0) {
				if (ret == -EAGAIN)
					return -2;
				if(!conn->nvme_pending) {
					printf("Connection close 3\n");
					ixev_close(&conn->ctx);
				}
				return -2;
			}
			if(ret==0)
				printf("fhmm ret is zero\n");

			conn->tx_sent += ret;
		}
	}
	conn->tx_sent = 0;
	conn->tx_pending = false;
	return 0;
}

int send_pending_client_reqs(struct pp_conn *conn) 
{
	int sent_reqs = 0;
	
	while(!list_empty(&conn->pending_requests)) {
		int ret;
		struct nvme_req *req = list_top(&conn->pending_requests, struct nvme_req, link);
		req->sent_time = rdtsc();
		ret = send_client_req(req);
		if(!ret) {
			sent_reqs++;
			list_pop(&conn->pending_requests, struct nvme_req, link);
			conn->list_len--;
		}
		else
			return ret;//sent_reqs;
	}
	return sent_reqs;
}

void send_handler(void * arg, int num_req)
{
	struct nvme_req *req;
	struct ixev_ctx *ctx = (struct ixev_ctx *)arg;
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	unsigned long now;
	int ssents = 0;

	int send_cond, measure_cond; 
	if (!SWEEP && qdepth) {
		time(&curr_time);
		if (difftime(curr_time, start_time) > run_time)
                        return;
	
		send_cond = num_req;
		measure_cond = 1;
	} else { 
	        if (sent == NUM_MEASURE * 3)
                        return;
                if (((now = rdtsc()) - last_send) < (cycles_between_req ))
                        return;
                if (sent == NUM_MEASURE)
                        phase_start = now;
                if (sent == 0)
                        bench_start = now;	

		send_cond = ((now - bench_start) / cycles_between_req) >= sent && sent < (NUM_MEASURE * 3);
		measure_cond = sent >= NUM_MEASURE && sent < NUM_MEASURE * 2;
	}


	while (send_cond) {
		ssents++;
		if(ssents > 32) {
			//never send more than max batch size
			break;
		}

		//setup next request
		req = mempool_alloc(&req_pool);
		if (!req) {
			receive_req(conn);
			//limited qd, if we run out of req, try again later
			break;
		}

		ixev_nvme_req_ctx_init(&req->ctx);
		req->lba_count = req_size;
		req->conn = conn;

		req->buf = mempool_alloc(&nvme_req_buf_pool);

		if ((rand() % 99) < read_percentage)
			req->cmd = CMD_GET;
		else
			req->cmd = CMD_SET;

		if (preconditioning)
			req->cmd = CMD_SET;
		//only do aligned accesses
		if (!sequential) {
			req->lba = rand() % (ns_size >> intlog2(ns_sector_size));
			//align
			req->lba = req->lba & ~7;
		}
		else {
			conn->seq_count += req_size;
			req->lba = conn->seq_count;
			assert (req->lba < ns_size / ns_sector_size);
			if ((req->lba % (((ns_size / ns_sector_size) / req_size)/ 100)) == 0)
				printf("lba %lu %lu %lu\n", req->lba, NUM_MEASURE, ns_size / ns_sector_size);
		}
		conn->list_len++;
		list_add_tail(&conn->pending_requests, &req->link);

		if (sent == NUM_MEASURE) {
			phase_start = rdtsc();
			__sync_synchronize();
		}
		
		if (measure_cond) {
			//missed send?
			if ((rdtsc() - last_send)  > (cycles_between_req * 105)/100) {
				missed_sends++;
			}
		}


		req->sent_time = rdtsc();
		last_send = now;
		sent++;

		if (!SWEEP && qdepth) {
			send_cond--;
		} else { 
			send_cond = ((now - bench_start) / cycles_between_req) >= sent && sent < (NUM_MEASURE * 3);
		}
	}

	send_pending_client_reqs(conn);
}

static void main_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	
	if(reason == IXEVOUT) {
		send_pending_client_reqs(conn);
	}
	else if(reason == IXEVHUP) {
		printf("Connection close 5\n");
		ixev_close(ctx);
		return;
	}
	receive_req(conn);
}

static void pp_dialed(struct ixev_ctx *ctx, long ret)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);	
	unsigned long now = rdtsc();
	
	ixev_set_handler(&conn->ctx, IXEVIN | IXEVOUT | IXEVHUP, &main_handler);
	running = true;
	if (tid == 0){
		printf("RqIOPS:\t IOPS:\t Avg:\t 10th:\t 20th:\t 30th:\t 40th:\t 50th:\t 60th:\t 70th:\t 80th:\t 90th:\t 95th:\t 99th:\t max:\t missed:\n");
	}
	
	conn_opened++;

	while(rdtsc() < now + 1000000) {} 
	
	return; 
}

static void pp_release(struct ixev_ctx *ctx)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	conn_opened--;
	if(conn_opened==0)
		printf("Tid: %lx All connections released handle %lx open conns still %i\n", pthread_self(), conn->ctx.handle, conn_opened);
	mempool_free(&pp_conn_pool, conn);
	terminate = true;
	running = false;
}

static struct ixev_ctx *pp_accept(struct ip_tuple *id)
{
	return NULL;
}


static struct ixev_conn_ops pp_conn_ops = {
	.accept		= &pp_accept,
	.release	= &pp_release,
	.dialed         = &pp_dialed,
};

static void* receive_loop(void *arg)
{
	int ret, i;
	int flags;
	int num_tests;

	tid = *(int *)arg;
	conn_opened = 0;	
	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		return NULL;
	};

	ret = mempool_create(&pp_conn_pool, &pp_conn_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	ret = mempool_create(&req_pool, &nvme_usr_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	ret = mempool_create(&nvme_req_buf_pool, &nvme_req_buf_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	struct pp_conn *conn = mempool_alloc(&pp_conn_pool);
	if (!conn) {
		printf("MEMPOOL ALLOC FAILED !\n");
		return NULL;
	}
	
	for(int i = 0; i < MAX_LATENCY; i++){
		measurements[i] = 0;
	}
	
	list_head_init(&conn->pending_requests);
	conn->rx_received = 0;
	conn->rx_pending = false;
	conn->tx_sent = 0;
	conn->tx_pending = false;
	conn->in_flight_pkts = 0x0UL;
	conn->sent_pkts = 0x0UL;
	conn->list_len = 0x0UL;
	conn->receive_loop = true;
	conn->seq_count = 0;
	
	ixev_ctx_init(&conn->ctx);
	
	conn->nvme_fg_handle = 0; //set to this for now

	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	ixev_dial(&conn->ctx, ip_tuple[tid]);
	if (preconditioning)
		SWEEP = 0;
	if (!SWEEP)
		num_tests = 1;
	else
		num_tests = NUM_TESTS;
	while (!running)
		ixev_wait();
	for (i = 0; i < num_tests; i++) {
		terminate = false;
		assert(sent == 0);
		assert(measure == 0);
		if (SWEEP) {
			cycles_between_req = (((unsigned long)cycles_per_us * 1000UL * 1000UL) / (sweep[i] / nr_threads));	
			NUM_MEASURE = sweep[i] * DURATION / nr_threads;
		}
		else { 
			cycles_between_req = ((unsigned long)cycles_per_us * 1000UL * 1000UL * nr_threads) / global_target_IOPS;	
			NUM_MEASURE = global_target_IOPS * DURATION / nr_threads;
		}
		assert(NUM_MEASURE <= MAX_NUM_MEASURE);
		if (preconditioning) //write each lba once
			NUM_MEASURE = (ns_size / ns_sector_size) / req_size;
		pthread_barrier_wait(&barrier);
	 
		time(&start_time);
		//---
		if (!SWEEP && qdepth){
			if (running)
				send_handler(&conn->ctx, qdepth);
			while(1){
				ixev_wait(); 
				if (terminate)
					break;	
			}
		} else {
			while (1) {
				if (running)
					send_handler(&conn->ctx, 0);
				ixev_wait();
				if (terminate)
					break;
			}
		}
		//---
	}
	running = true;
	ixev_close(&conn->ctx);
	
	while (running)
		ixev_wait();
	
	return NULL;
}

int reflex_client_main(int argc, char *argv[])
{
	int ret;
	unsigned int pp_conn_pool_entries;
	int nr_cpu, req_size_bytes;
	pthread_t thread[64];
	int tid[64];
 	int i;
	
	sleep(10);

	nr_cpu = cpus_active;
	if (nr_cpu < 1) {
		fprintf(stderr, "got invalid cpu count %d\n", nr_cpu);
		exit(-1);
	}

        int opt;

        while ((opt = getopt(argc, argv, "s:p:w:T:i:r:S:R:P:d:t:h")) != -1) {
                switch (opt) {
			case 's' :
				ip = malloc(sizeof(char) * strlen(optarg));
				strcpy(ip, optarg);
				break;
			case 'p' :
				port = atoi(optarg);
				break;
                        case 'w' :
				if (strcmp(optarg, "seq") == 0)
					sequential = 1;
				else if (strcmp(optarg, "rand") == 0)
					sequential = 0;
				else {
					sequential = 0;
					printf("WARNING: invalid workload type, use random by default\n");
				}
				break;
                        case 'T' :
                                nr_threads = atoi(optarg);
                                break;
                        case 'i' :
                                global_target_IOPS = atoi(optarg);
                                break;
                        case 'r' :
                                read_percentage = atoi(optarg);
                                break;
                        case 'S' :
                                SWEEP = atoi(optarg);
                                break;
                        case 'R' :
                                req_size_bytes = atoi(optarg);
                                if (req_size_bytes % ns_sector_size != 0){
                                        printf("WARNING: request size should be multiple of sector size\n");
                                }
				req_size = req_size_bytes / ns_sector_size;
                                break;
                        case 'P' :
                                preconditioning = atoi(optarg);
                                break;
                        case 'd' :
                                qdepth = atoi(optarg);
                                break;
                        case 't' :
                                run_time = atoi(optarg);
                                break;
			case 'h' :
				fprintf(stderr, "\nUsage: \n"
					"sudo ./dp/ix\n"
					"to run ReFlex server, no parameters required\n"
					"to run ReFlex client, set the following options:\n"
					"-s  server IP address\n"
					"-p  server port number\n"
					"-w  workload type [seq/rand] (default=rand)\n"
					"-T  number of threads (default=1)\n"
					"-i  target IOPS for open-loop test (default=50000)\n"
					"-r  percentage of read requests (default=100)\n"
					"-S  sweep multiple target IOPS for open-loop test (default=1)\n"
					"-R  request size in bytes (default=1024)\n"
					"-P  precondition (default=0)\n"
					"-d  queue depth for closed-loop test (default=0)\n"
					"-t  execution time in seconds for closed-loop test (default=0)\n"
				);
				exit(1);
                        default:
                                fprintf(stderr, "invalid command option\n");
                                exit(1);
                }
        }

	printf("DEBUG: ip=%s, port=%d, seq=%d, nr_threads=%d, global=%d, read=%d, SWEEP=%d, req_size_bytes=%d, preconditioning=%d, qdepth=%d, run_time=%d\n", ip, port, sequential, nr_threads, global_target_IOPS, read_percentage, SWEEP, req_size_bytes, preconditioning, qdepth, run_time);

	if (ip == NULL) {
		fprintf(stderr, "missing server IP address, enter -s [ip] to specify\n");
		exit(1);
	}
	if (port == -1) {
		fprintf(stderr, "missing port number, enter -p [port] to specify\n");
		exit(1);
	}
	if (SWEEP && qdepth) {
		fprintf(stderr, "SWEEP and qdepth cannot both be nonzero\n");
		exit(1);
	}

	assert(nr_threads <= nr_cpu);
	pthread_barrier_init(&barrier, NULL, nr_threads);
	
	for (int i = 0; i < nr_threads; i++) {
		ip_tuple[i] = malloc(sizeof(struct ip_tuple[i]));
		if (!ip_tuple[i])
			exit(-1);

		if (parse_ip_addr(ip, &ip_tuple[i]->dst_ip)) {
			fprintf(stderr, "Bad IP address '%s'", ip);
			exit(1);
		}

		timer_calibrate_tsc();
		
		ip_tuple[i]->dst_port = port + i;
		ip_tuple[i]->src_port = port;
		printf("Connecting to port: %i\n", port + i);
	}

	free(ip);
	cycles_between_req = ((unsigned long)cycles_per_us * 1000UL * 1000UL * nr_threads) / global_target_IOPS;
	NUM_MEASURE = global_target_IOPS * DURATION / nr_threads;
	pp_conn_pool_entries = 16 * 4096;
	pp_conn_pool_entries = ROUND_UP(pp_conn_pool_entries, MEMPOOL_DEFAULT_CHUNKSIZE);
	ixev_init(&pp_conn_ops);
	ret = mempool_create_datastore(&pp_conn_datastore, pp_conn_pool_entries,
				       sizeof(struct pp_conn), "pp_conn");
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return ret;
	}

	ret = mempool_create_datastore(&nvme_usr_datastore, 
				       outstanding_reqs * 2,
				       sizeof(struct nvme_req),  "nvme_req_1");
	if (ret) {
		fprintf(stderr, "unable to create datastore\n");
		return ret;
	}

	ret = mempool_create_datastore(&nvme_req_buf_datastore,
				       // *2 avoids out-of-mem error
				       outstanding_reqs * 2,
				       req_size_bytes, "nvme_req_2");
	if (ret) {
		fprintf(stderr, "unable to create datastore\n");
		return ret;
	}

       	/*
	sys_spawnmode(true);
	for (int i = 1; i < nr_threads; i++) {
		tid[i] = i;
		if (pthread_create(&thread[i], NULL, receive_loop, &tid[i])) {
			fprintf(stderr, "failed to spawn thread %d\n", i);
			exit(-1);
		}
	}
	*/

	for (i = 1; i < nr_cpu; i++) {
		//ret = pthread_create(&tid, NULL, start_cpu, (void *)(unsigned long) i);
		log_info("rte_eal_remote_launch...receive_loop\n");
		tid[i] = i;
		//ret = rte_eal_remote_launch(receive_loop, (void *)(unsigned long) i, i);		
		ret = rte_eal_remote_launch(receive_loop, &tid[i], i);		

		if (ret) {
			log_err("init: unable to start app\n");
			return -EAGAIN;
		}
	}

	printf("Started %i threads\n", nr_cpu);
	tid[0] = 0;

	receive_loop(&tid[0]);
	/*
	for (int i = 1; i < nr_threads; i++) {
		pthread_join(thread[i], NULL);
	}
	printf("joined. Total IOPS: %lu\n", iops);
	*/
	return 0;
}


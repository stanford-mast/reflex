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
#include <netinet/in.h>
#include <fcntl.h>

#include <ixev.h>
#include <ixev_timer.h>
#include <ix/mempool.h>
#include <ix/list.h>
#include <ix/cfg.h>

#include "reflex.h" 

/****monitoring*********/
#include <ix/monitor.h>
#include <signal.h>
#include <unistd.h>

struct util *util_per_sec;
struct list_head *util_list; 
unsigned long start_time;
/***********************/

#define ROUND_UP(num, multiple) ((((num) + (multiple) - 1) / (multiple)) * (multiple))
#define BATCH_DEPTH  512
#define NAMESPACE 0

#define BINARY_HEADER binary_header_blk_t
#define CTRL_MSG_HEADER ctrl_msg_header_t

#define NVME_ENABLE

#define MAX_PAGES_PER_ACCESS 256 //64
#define PAGE_SIZE 4096

#define MAX_NUM_CONTIG_ALLOC_RETRIES 5

#define CONTROLLER_STAT_SEC_INTERVAL 1 //how frequently to send stats to ctrl (in sec)
#define UTIL_STAT_CMD 15

static int outstanding_reqs = 4096 * 64; 
static int outstanding_req_bufs = 4096 * 64; //4096 * 64;
static unsigned long ns_size;
static unsigned long ns_sector_size;

static struct mempool_datastore nvme_req_buf_datastore;
static __thread struct mempool nvme_req_buf_pool;

static struct mempool_datastore nvme_req_datastore;
static __thread struct mempool nvme_req_pool;
static __thread int conn_opened;
static __thread long reqs_allocated = 0;

struct nvme_req {
	struct ixev_nvme_req_ctx ctx;
	unsigned int lba_count;
	unsigned long lba;
	uint16_t opcode;
	struct pp_conn *conn;
	struct list_node link;
	struct ixev_ref ref;				//for zero-copy
	unsigned long timestamp;
	void *remote_req_handle;
	char *buf[MAX_PAGES_PER_ACCESS]; 	//nvme buffer to read/write data into
	int current_sgl_buf;
};

struct pp_conn {
	struct ixev_ctx ctx;
	size_t rx_received; //the amount of data received/sent for the current ReFlex request
	size_t tx_sent;
	bool rx_pending; 	//is there a ReFlex req currently being received/sent
	bool tx_pending;
	int nvme_pending;
	long in_flight_pkts;
	long sent_pkts;
	long list_len;
	unsigned long req_received;
	struct list_head pending_requests;
	long nvme_fg_handle; //nvme flow group handle
	struct nvme_req *current_req;
	char data_send[sizeof(BINARY_HEADER)]; //use zero-copy for payload
	char data_recv[sizeof(BINARY_HEADER)]; //use zero-copy for payload
};


struct controller_conn {
	struct ixev_ctx ctx;
	size_t rx_received; //the amount of data received/sent for the current request
	size_t tx_sent;
	bool rx_pending; 	//is there a req currently being received/sent
	bool tx_pending;
	long in_flight_pkts;
	long sent_pkts;
	long list_len;
	unsigned long req_received;
	struct list_head pending_requests;
	//struct nvme_req *current_req;
	char data_send[sizeof(CTRL_MSG_HEADER)]; //use zero-copy for payload
	//char data_recv[sizeof(CTRL_MSG_HEADER)]; //use zero-copy for payload
};

// Connection for communication with Pocket controller
struct controller_conn *ctrl_conn;
struct ip_tuple ctrl_ip_tuple;

static struct mempool_datastore pp_conn_datastore;
static __thread struct mempool pp_conn_pool;

static __thread hqu_t handle; 


static void pp_main_handler(struct ixev_ctx *ctx, unsigned int reason);
static void ctrl_main_handler(struct ixev_ctx *ctx, unsigned int reason);

#define MAKE_IP_ADDR(a, b, c, d)			\
	(((uint32_t) a << 24) | ((uint32_t) b << 16) |	\
	 ((uint32_t) c << 8) | (uint32_t) d)

static int parse_ip_addr(const char *str, uint32_t *addr)
{
	unsigned char a, b, c, d;

	if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
		return -EINVAL;

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}


static void send_completed_cb(struct ixev_ref *ref)
{
	struct nvme_req *req = container_of(ref, struct nvme_req, ref);
	struct pp_conn *conn = req->conn;
	int i, num4k;
	
	num4k = (req->lba_count * ns_sector_size) / 4096;
	if (((req->lba_count * ns_sector_size) % 4096) != 0)
		num4k++;
	for (i = 0; i < num4k; i++){
		mempool_free(&nvme_req_buf_pool, req->buf[i]); 
	}

	mempool_free(&nvme_req_pool, req);
	reqs_allocated--;
}

/*
 * returns 0 if send was successfull and -1 if tx path is busy
 */
int send_req(struct nvme_req *req) 
{
	struct pp_conn *conn = req->conn;
	int ret = 0;
	BINARY_HEADER *header;

	if(!conn->tx_pending){
		//setup header
		header = (BINARY_HEADER *)&conn->data_send[0];
		header->magic = sizeof(BINARY_HEADER); //RESP_PKT;
		header->opcode = req->opcode;
		header->lba = req->lba;
		
		if (req->opcode == CMD_SET)
			header->lba_count = 0;
		else
			header->lba_count = req->lba_count;
		header->req_handle = req->remote_req_handle;

		assert(header->req_handle); 

		while (conn->tx_sent < (sizeof(BINARY_HEADER))) {
			ret = ixev_send(&conn->ctx, &conn->data_send[conn->tx_sent], sizeof(BINARY_HEADER) - conn->tx_sent);
			if (ret == -EAGAIN)
				return -1;
			
			if (ret < 0) {
				if(!conn->nvme_pending) {
					ixev_close(&conn->ctx);
				}
				return -2;
				ret = 0;
			}
			conn->tx_sent += ret;
		}
	
		conn->tx_pending = true;
		conn->tx_sent = 0;
	}
	ret = 0;
	if (req->opcode == CMD_GET) {
		while (conn->tx_sent < req->lba_count * ns_sector_size) {		
			int to_send = min(PAGE_SIZE - (conn->tx_sent % PAGE_SIZE),
					  (req->lba_count * ns_sector_size) - conn->tx_sent);
		
			ret = ixev_send_zc(&conn->ctx,
					   &req->buf[req->current_sgl_buf][conn->tx_sent % PAGE_SIZE],
					   to_send); 
			if (ret < 0) {
				if (ret == -EAGAIN) 
					return -1;

				if(!conn->nvme_pending) {
					printf("Connection close 3\n");
					ixev_close(&conn->ctx);
				}
				return -2;
			}
			if(ret==0)
				printf("fhmm ret is zero\n");

			conn->tx_sent += ret;
			if ((conn->tx_sent % PAGE_SIZE) == 0)
				req->current_sgl_buf++;
		}
		assert(req->current_sgl_buf <= req->lba_count);
		req->ref.cb = &send_completed_cb;
		req->ref.send_pos = req->lba_count * ns_sector_size;
		ixev_add_sent_cb(&conn->ctx, &req->ref);
	}
	else { //PUT
		int i, num4k;
		num4k = (req->lba_count * ns_sector_size) / 4096;
		if (((req->lba_count * ns_sector_size) % 4096) != 0)
			num4k++;
		for (i = 0; i < num4k; i++) {
			mempool_free(&nvme_req_buf_pool, req->buf[i]);
		}
		mempool_free(&nvme_req_pool, req);
		reqs_allocated--;
		//conn->sent_pkts--;
	}
	conn->list_len--;
	conn->tx_sent = 0;
	conn->tx_pending =false;
	return 0;
}

int send_pending_reqs(struct pp_conn *conn) 
{
	int sent_reqs = 0;
	
	while(!list_empty(&conn->pending_requests)) {
		struct nvme_req *req = list_top(&conn->pending_requests, struct nvme_req, link);
		int ret = send_req(req);
		if(!ret) {
			sent_reqs++;
			list_pop(&conn->pending_requests, struct nvme_req, link);
		}
		else
			return sent_reqs;
	}
	return sent_reqs;
}

static void nvme_written_cb(struct ixev_nvme_req_ctx *ctx, unsigned int reason) 
{
	struct nvme_req *req = container_of(ctx, struct nvme_req, ctx);
	struct pp_conn *conn = req->conn;
	/*
	int num_bytes = req->lba_count * 512;
	int num_4kbufs = num_bytes /4096 + 1;
	printf("\n***WRITTEN: num_bytes %d, lba_count %u \n", num_bytes, req->lba_count);
    int i, j;

	for (i =0; i < num_4kbufs; i++){
		for (j=0; j < 4096; j++){
			if (num_bytes > i*4096 + j) {
				//printf("%x ", *(req->buf[i]+j));
				printf("%c", *(req->buf[i]+j));
			}
		}
	}
	printf("\n");
	*/
	conn->list_len++;
	conn->in_flight_pkts--;
	conn->sent_pkts++;
	list_add_tail(&conn->pending_requests, &req->link);
	send_pending_reqs(conn);
	return;
}

static void nvme_response_cb(struct ixev_nvme_req_ctx *ctx, unsigned int reason)
{
	struct nvme_req *req = container_of(ctx, struct nvme_req, ctx);
	struct pp_conn *conn = req->conn;
	
/*
	int num_bytes = req->lba_count * 512;
	int num_4kbufs = num_bytes /4096 + 1;
	printf("\n****READ: num_bytes %d, lba_count %u \n", num_bytes, req->lba_count);
    int i, j;

	for (i =0; i < num_4kbufs; i++){
		for (j=0; j < 4096; j++){
			if (num_bytes > i*4096 + j) {
				//printf("%x ", *(req->buf[i]+j));
				printf("%c-", *(req->buf[i]+j));
			}
			else{
				printf("\n i is %d, j %d\n", i, j);
				break;
			}
		}
	}
*/
	conn->list_len++;
	conn->in_flight_pkts--;
	conn->sent_pkts++;
	list_add_tail(&conn->pending_requests, &req->link);
	send_pending_reqs(conn);
	return;
}

static void nvme_opened_cb(hqu_t _handle, unsigned long _ns_size, unsigned long _ns_sector_size)
{
	ns_size = _ns_size;
	ns_sector_size = _ns_sector_size;
	if(ns_size){
		handle = _handle;
	}
}


static void nvme_registered_flow_cb(long fg_handle, struct ixev_ctx* ctx, long ret)
{
	if(ret < 0){
		printf("ERROR: couldn't register flow\n");
		//probably signifies you need a less strict SLO
	}
	
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	conn->nvme_fg_handle = fg_handle;
}

static void nvme_unregistered_flow_cb(long flow_group_id , long ret)
{

	if(ret){
		printf("ERROR: couldn't unregister flow\n");
	}
}

static struct ixev_nvme_ops nvme_ops = {
	.opened    = &nvme_opened_cb,
	.registered_flow    = &nvme_registered_flow_cb,
	.unregistered_flow    = &nvme_unregistered_flow_cb,
};

static void receive_req(struct pp_conn *conn)
{
	ssize_t ret;
	struct nvme_req *req;
	BINARY_HEADER* header;
	void* nvme_addr;
	
	while(1) {
		int num4k;
		if(!conn->rx_pending) {
			int i;
						
			ret = ixev_recv(&conn->ctx, &conn->data_recv[conn->rx_received],
					sizeof(BINARY_HEADER) - conn->rx_received); 
			if (ret <= 0) {
				if (ret != -EAGAIN) {
					if(!conn->nvme_pending) {
						printf("Connection close 6\n");
						ixev_close(&conn->ctx);
					}
				}
				return;
			}
			else
				conn->rx_received += ret;

			if(conn->rx_received < sizeof(BINARY_HEADER))
				return;
			
			//received the header
			conn->current_req = mempool_alloc(&nvme_req_pool);
			if (!conn->current_req) {
				printf("Cannot allocate nvme_usr req. In flight requests: %lu sent req %lu . list len %lu \n", conn->in_flight_pkts, conn->sent_pkts, conn->list_len);
				return;
			}
			conn->current_req->current_sgl_buf = 0;
			//allocate lba_count sector sized nvme bufs
			header = (BINARY_HEADER *)&conn->data_recv[0];
			
			assert(header->magic == sizeof(BINARY_HEADER));

			num4k = (header->lba_count * ns_sector_size) / 4096;
			assert(num4k <= MAX_PAGES_PER_ACCESS);
			if (((header->lba_count * ns_sector_size) % 4096) != 0)
				num4k++;

			void* req_buf_array[num4k];
			for (i = 0; i < num4k; i++) {
				req_buf_array[i] = mempool_alloc(&nvme_req_buf_pool);
				if (req_buf_array[i] == NULL){
					printf("ERROR: alloc of nvme_req_buf failed\n");
					assert(0);
				}
				conn->current_req->buf[i] = req_buf_array[i];
				//printf("req_buf_array[%d] is %p, expect next %x\n", i, req_buf_array[i], (uint64_t)(req_buf_array[i]) +4096); 
			}
			//printf("req buf is %p\n", conn->current_req->buf[0]);
 
			ixev_nvme_req_ctx_init(&conn->current_req->ctx);

			reqs_allocated++;
			conn->rx_pending = true;
			conn->rx_received = 0;
		}


		req = conn->current_req;
		header = (BINARY_HEADER *)&conn->data_recv[0];
		
		assert(header->magic == sizeof(BINARY_HEADER));
		
		if (header->opcode == CMD_SET) {
			while (conn->rx_received < header->lba_count * ns_sector_size) {		
				int to_receive = min(PAGE_SIZE - (conn->rx_received % PAGE_SIZE),
						  (header->lba_count * ns_sector_size) - conn->rx_received);
				
				ret = ixev_recv(&conn->ctx,
						&req->buf[req->current_sgl_buf][conn->rx_received % PAGE_SIZE],
						to_receive); 
				
				if (ret < 0) {
					if (ret == -EAGAIN) 
						return;
					
					if(!conn->nvme_pending) {
						printf("Connection close 3\n");
						ixev_close(&conn->ctx);
					}
					return;
				}

				conn->rx_received += ret;
				if ((conn->rx_received % PAGE_SIZE) == 0){
					req->current_sgl_buf++;
				}
			}
			//4KB sgl bufs should match number of 512B sectors
			assert(req->current_sgl_buf <= header->lba_count * 8);

		}
		else if (header->opcode == CMD_GET) {}
		else {
			printf("Received unsupported command, closing connection\n");
			ixev_close(&conn->ctx);
			return;
		}

		req->opcode = header->opcode;
		req->lba_count = header->lba_count;
		req->lba = header->lba;
		req->remote_req_handle = header->req_handle;
				
		req->ctx.handle = handle;
		req->conn = conn;

		nvme_addr = (void*)(header->lba << 9); 
		assert((unsigned long)nvme_addr < ns_size); 
		
		conn->in_flight_pkts++;
		num4k = (header->lba_count * ns_sector_size) / PAGE_SIZE;
		if (((header->lba_count * ns_sector_size) % PAGE_SIZE) != 0)
			num4k++;
		
		switch (header->opcode) {
		case CMD_SET:
			util_per_sec->num_req++; //monitoring CPU 
			
			ixev_set_nvme_handler(&req->ctx, IXEV_NVME_WR, &nvme_written_cb);
			//ixev_nvme_write(conn->nvme_fg_handle, req->buf[0], header->lba, header->lba_count, (unsigned long)&req->ctx);
			ixev_nvme_writev(conn->nvme_fg_handle, (void**)&req->buf[0], num4k,
					header->lba, header->lba_count, (unsigned long)&req->ctx);
			conn->nvme_pending++;	
			break;
		case CMD_GET:
			util_per_sec->num_req++; //monitoring CPU 
			
			ixev_set_nvme_handler(&req->ctx, IXEV_NVME_RD, &nvme_response_cb);
			//ixev_nvme_read(conn->nvme_fg_handle, req->buf[0], header->lba, header->lba_count, (unsigned long)&req->ctx);
			ixev_nvme_readv(conn->nvme_fg_handle, (void**)&req->buf[0], num4k,
					header->lba, header->lba_count, (unsigned long)&req->ctx);
			conn->nvme_pending++;	
			break;
		default:
			printf("Received illegal msg - dropping msg\n");
			mempool_free(&nvme_req_pool, req);
			reqs_allocated--;
		}
		conn->rx_received = 0;
		conn->rx_pending = false;
	}
}

static void pp_main_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);

	//Lets always try to send
	if(true || reason==IXEVOUT) {
		send_pending_reqs(conn);
	}
	if(reason==IXEVHUP) {
		ixev_nvme_unregister_flow(conn->nvme_fg_handle);
		ixev_close(&conn->ctx);
		return;
	}
	receive_req(conn);
}

static struct ixev_ctx *pp_accept(struct ip_tuple *id)
{
	unsigned long cookie;
	unsigned int latency_us_SLO = 0;
	unsigned long IOPS_SLO = 0;
	int rd_wr_ratio_SLO = 50;
	
	struct pp_conn *conn = mempool_alloc(&pp_conn_pool);
	if (!conn) {
		printf("MEMPOOL ALLOC FAILED !\n");
		return NULL;
	}
	list_head_init(&conn->pending_requests);
	conn->rx_received = 0;
	conn->rx_pending = false;
	conn->tx_sent = 0;
	conn->tx_pending = false;
	conn->in_flight_pkts = 0x0UL;
	conn->sent_pkts = 0x0UL;
	conn->list_len = 0x0UL;
	conn->req_received = 0;
	ixev_ctx_init(&conn->ctx);
	ixev_set_handler(&conn->ctx, IXEVIN | IXEVOUT | IXEVHUP, &pp_main_handler);
	conn_opened++;

	conn->nvme_fg_handle = 0; //set to this for now
	cookie = (unsigned long) &conn->ctx;

	/****************************************/
	/* LATENCY SLO POLICIES FOR FLOW GROUPS */
	switch (id->dst_port) {

	case 1234:
		latency_us_SLO = 0; //best-effort
		IOPS_SLO = 0;
		rd_wr_ratio_SLO = 100;
		break; 
	case 1235:
		latency_us_SLO = 0; //best-effort
		IOPS_SLO = 0;
		rd_wr_ratio_SLO = 100;
		break;
	case 1236:
		latency_us_SLO = 0; //best-effort
		IOPS_SLO = 0;
		rd_wr_ratio_SLO = 100;
		break;
	case 1237:
		latency_us_SLO = 0; //best-effort
		IOPS_SLO = 0;
		rd_wr_ratio_SLO = 100;
		break;
	case 5678: 
		latency_us_SLO = 1000; //latency-critical
		IOPS_SLO = 120000;
		rd_wr_ratio_SLO = 100;
		break;
	case 5679: 
		latency_us_SLO = 1000; //latency-critical
		IOPS_SLO = 70000;
		rd_wr_ratio_SLO = 80;
		break;

	default:
		printf("WARNING: unrecognized SLO policy, default is best-effort\n");
		break;
	}
	/*
	 * FIXME: add support for dynamic SLO registration by client
	 * Current hack: associate a port with an SLO (defined in case statement above)
	 * Client communicates with server using dst_port that corresponds to its SLO
	 */
	ixev_nvme_register_flow(id->dst_port, cookie, latency_us_SLO, IOPS_SLO, rd_wr_ratio_SLO);
	return &conn->ctx;
}

static void pp_release(struct ixev_ctx *ctx)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	conn_opened--;
	
	mempool_free(&pp_conn_pool, conn);
}

static void pp_dialed(struct ixev_ctx *ctx, long ret)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);	
	unsigned long now = rdtsc();
	
	ixev_set_handler(&conn->ctx, IXEVIN | IXEVOUT | IXEVHUP, &ctrl_main_handler);
	
	conn_opened++;

	while(rdtsc() < now + 1500000) {} 
	printf("Dialed\n");	
	return; 
}


static void ctrl_main_handler(struct ixev_ctx *ctx, unsigned int reason)
{
	struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
	
	//if(reason == IXEVOUT) {
	//	ctrl_send_pending_client_reqs(conn);
	//}
	if(reason == IXEVHUP) {
		printf("Ctrl connection closing\n");
		ixev_close(ctx);
		return;
	}
	//ctrl_receive_req(conn); //TODO: check if need to process ctrl ACKS?
}

static struct ixev_conn_ops pp_conn_ops = {
	.accept		= &pp_accept,
	.release	= &pp_release,
	.dialed		= &pp_dialed,
};



static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t launch_cond = PTHREAD_COND_INITIALIZER;

struct launch_req {
	struct launch_req *next;
};

static struct launch_req *launch_reqs;

void send_ctrl_stats(void) {
	CTRL_MSG_HEADER *header;
	int ret;
	char *ip = NULL;

	header = (CTRL_MSG_HEADER *) &ctrl_conn->data_send[0];
        header->msglen = htonl(sizeof(CTRL_MSG_HEADER)); //note: adjust this if move to multi-core cpu util
	header->ticket = htonl(rdtsc());
	header->util_stat_cmd = htons(UTIL_STAT_CMD);
	header->ipaddr = htonl(CFG.host_addr.addr);
	header->port = htonl(1234);
	int rxMbps = 0;
	int txMbps = 0;
	//printf("send stat: %d, %d\n", util_per_sec->rxbytes, util_per_sec->txbytes);
	if (!list_empty(util_list)){
		struct util *u = list_tail(util_list, struct util, link);
		printf("send stat: %d, %d\n", u->rxbytes, u->txbytes);
		rxMbps = u->rxbytes * 8 / 1e6;
		txMbps = u->txbytes * 8 / 1e6;
	}
	header->rxMbps = htonl(rxMbps);
	header->txMbps = htonl(txMbps);
	header->cpu_util_len = htonl(1);
	header->cpu_util = htonl(100);

	while (ctrl_conn->tx_sent < sizeof(CTRL_MSG_HEADER)){
		ret = ixev_send(&ctrl_conn->ctx, &ctrl_conn->data_send[ctrl_conn->tx_sent],
				sizeof(CTRL_MSG_HEADER) - ctrl_conn->tx_sent);
		if (ret == -EAGAIN){
			return -1;
		}
		if (ret < 0) {
			return -2;
		}
		ctrl_conn->tx_sent += ret;
	}
	assert(ctrl_conn->tx_sent == sizeof(CTRL_MSG_HEADER));
	ctrl_conn->tx_pending = true;
	ctrl_conn->tx_sent = 0;
}


void *pp_main(void *arg)
{
	int ret;
	int flags;
	conn_opened = 0;
	printf("pp_main on cpu %d, thread self is %x\n", percpu_get(cpu_nr),pthread_self());
	struct launch_req *req;

	ret = ixev_init_thread();
	if (ret) {
		fprintf(stderr, "unable to init IXEV\n");
		return NULL;
	};

	ret = mempool_create(&nvme_req_pool, &nvme_req_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	
	ret = mempool_create(&nvme_req_buf_pool, &nvme_req_buf_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}
	

	ret = mempool_create(&pp_conn_pool, &pp_conn_datastore, MEMPOOL_SANITY_GLOBAL, 0);
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return NULL;
	}

	ixev_nvme_open(NAMESPACE, 1);

	// Create conn that will use to send stats to Pocket controller
	ctrl_conn = mempool_alloc(&pp_conn_pool);
	if (!ctrl_conn) {
		printf("MEMPOOL ALLOC FAILED !\n");
		return NULL;
	}
	
	list_head_init(&ctrl_conn->pending_requests);
	ctrl_conn->rx_received = 0;
	ctrl_conn->rx_pending = false;
	ctrl_conn->tx_sent = 0;
	ctrl_conn->tx_pending = false;
	ctrl_conn->in_flight_pkts = 0x0UL;
	ctrl_conn->sent_pkts = 0x0UL;
	ctrl_conn->list_len = 0x0UL;
	
	ixev_ctx_init(&ctrl_conn->ctx);
	
	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	ctrl_ip_tuple.dst_ip = 167849906; // 10.1.47.178 -- this is hardcoded for now!
	ctrl_ip_tuple.dst_port = 2345;
	ctrl_ip_tuple.src_port = 1234;
	printf("Dial controller...\n");
	ixev_dial(&ctrl_conn->ctx, &ctrl_ip_tuple);

	while (1) {
		ixev_wait();
		unsigned long now = rdtsc();
		unsigned long time_elapsed = (now-start_time)/cycles_per_us;
		if (time_elapsed >= CONTROLLER_STAT_SEC_INTERVAL * 1000000) {
			send_ctrl_stats();
		}
	}

	return NULL;
}



void sig_handler(int signo)
{
    if (signo == SIGINT){
        printf("Savings logs to log.txt...\n");

        FILE *fp;
        fp = fopen("log.txt", "w");
        while(!list_empty(util_list)){
            struct util *u = list_top(util_list, struct util, link);
            /*
            fprintf(fp, "num_req/sec=%lu\n", u->num_req);
            fprintf(fp, "txbytes/sec=%lu\n", u->txbytes);
            fprintf(fp, "rxbytes/sec=%lu\n", u->rxbytes);
            */
            fprintf(fp, "%lu\t%lu\t%lu\n", u->num_req,u->txbytes,u->rxbytes);
            list_pop(util_list, struct util, link);
            free(u);
        }

        fclose(fp);

        free(util_per_sec);
        exit(0);
    }
}



int reflex_server_main(int argc, char *argv[])
{
	int i, nr_cpu;
	pthread_t tid;
	int ret;
	unsigned int pp_conn_pool_entries;

	nr_cpu = cpus_active; // sys_nrcpus();
	if (nr_cpu < 1) {
		fprintf(stderr, "got invalid cpu count %d\n", nr_cpu);
		exit(-1);
	}

	ret = mempool_create_datastore(&nvme_req_datastore, 
				       outstanding_reqs,
				       sizeof(struct nvme_req), "nvme_req_datastore");
	if (ret) {
		fprintf(stderr, "unable to create datastore\n");
		return ret;
	}
	
	pp_conn_pool_entries = ROUND_UP(16 * 4096, MEMPOOL_DEFAULT_CHUNKSIZE);

	ixev_init_conn_nvme(&pp_conn_ops, &nvme_ops);
	if (ret) {
		fprintf(stderr, "failed to initialize ixev nvme\n");
		return ret;
	}
	ret = mempool_create_datastore(&pp_conn_datastore, pp_conn_pool_entries, sizeof(struct pp_conn), "pp_conn");
	if (ret) {
		fprintf(stderr, "unable to create mempool\n");
		return ret;
	}
	
	ret = mempool_create_datastore_align(&nvme_req_buf_datastore, 
				       outstanding_req_bufs,
				       4096, "nvme_req_buf_datastore");

	if (ret) {
		fprintf(stderr, "unable to create datastore\n");
		return ret;
	}


	for (i = 1; i < nr_cpu; i++) {
		//ret = pthread_create(&tid, NULL, start_cpu, (void *)(unsigned long) i);
		log_info("rte_eal_remote_launch...pp_main\n");
		ret = rte_eal_remote_launch(pp_main, (void *)(unsigned long) i, i);		

		if (ret) {
			log_err("init: unable to start app\n");
			return -EAGAIN;
		}
	}

	printf("Started ReFlex server...\n");

	/******start monitoring***/
	util_per_sec = (struct util*) malloc(sizeof(struct util));
	util_list = (struct list_head*) malloc(sizeof(struct list_head));

	list_head_init(util_list);
	util_per_sec->num_req = 0;
	util_per_sec->rxbytes = 0;
	util_per_sec->txbytes = 0;
	start_time = rdtsc();

	if (signal(SIGINT, sig_handler) == SIG_ERR)
		printf("\ncan't catch SIGINT\n");
	/************************/

	pp_main(NULL);
	return 0;
}


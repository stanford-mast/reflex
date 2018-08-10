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
 * init.c - initialization
 */

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_ethdev.h>

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <ix/stddef.h>
#include <ix/log.h>
#include <ix/errno.h>
//#include <ix/pci.h>
#include <ix/ethdev.h>
#include <ix/ethqueue.h>
#include <ix/timer.h>
#include <ix/cpu.h>
#include <ix/mbuf.h>
#include <ix/syscall.h>
#include <ix/kstats.h>
#include <ix/profiler.h>
#include <ix/lock.h>
#include <ix/cfg.h>
#include <ix/control_plane.h>
#include <ix/log.h>

#include <ix/dpdk.h>

#include <net/ip.h>

#include "reflex.h"

#include <lwip/memp.h>

#define MSR_RAPL_POWER_UNIT 1542
#define ENERGY_UNIT_MASK 0x1F00
#define ENERGY_UNIT_OFFSET 0x08

static int init_parse_cpu(void);
static int init_cfg(void);
static int init_firstcpu(void);
static int init_hw(void);
static int init_ethdev(void);

extern int init_global_arrays(void);
extern int init_nvmedev(void);
extern int init_nvmeqp_cpu(void);
extern int init_nvme_request(void);
extern int init_nvme_request_cpu(void);

extern int net_init(void);
extern int tcp_api_init(void);
extern int tcp_api_init_cpu(void);
extern int tcp_api_init_fg(void);
extern int sandbox_init(int argc, char *argv[]);
extern void tcp_init(struct eth_fg *);
extern int cp_init(void);
extern int mempool_init(void);
extern int init_migration_cpu(void);
extern int dpdk_init(void);


struct init_vector_t {
	const char *name;
	int (*f)(void);
	int (*fcpu)(void);
	int (*ffg)(unsigned int);
};


static struct init_vector_t init_tbl[] = {
	{ "global_arrays", init_global_arrays, NULL, NULL},
	{ "CPU",     cpu_init,     NULL, NULL},
    { "cfgcpu",     init_parse_cpu,     NULL, NULL},            // after cpu  
	{ "dpdk",    dpdk_init,    NULL, NULL},
	{ "timer",   timer_init,   timer_init_cpu, NULL},
	{ "net",     net_init,     NULL, NULL},
	{ "cfg",     init_cfg,     NULL, NULL},              // after net
	{ "cp",      cp_init,      NULL, NULL},
	{ "firstcpu", init_firstcpu, NULL, NULL},             // after cfg
	{ "mbuf",    mbuf_init,    mbuf_init_cpu, NULL},      // after firstcpu
	{ "memp",    memp_init,    memp_init_cpu, NULL},
	{ "tcpapi",  tcp_api_init, tcp_api_init_cpu, NULL},
	{ "ethdev",  init_ethdev,  ethdev_init_cpu, NULL},
	{ "nvmemem", init_nvme_request, init_nvme_request_cpu, NULL},
	{ "migration", NULL, init_migration_cpu, NULL},
	{ "nvmedev", init_nvmedev, NULL, NULL},               // before per-cpu init
	{ "hw",      init_hw,      NULL, NULL},               // spaws per-cpu init sequence
	{ "nvmeqp",  NULL, init_nvmeqp_cpu, NULL},            // after per-cpu init
	{ "syscall", NULL,         syscall_init_cpu, NULL},
#ifdef ENABLE_KSTATS
	{ "kstats",  NULL,         kstats_init_cpu, NULL},    // after timer
#endif
	{ NULL, NULL, NULL, NULL}
};


static int init_argc;
static char **init_argv;
static int args_parsed;

volatile int uaccess_fault;

bool PROCESS_SHOULD_BE_SECONDARY = false;


static struct rte_eth_conf default_eth_conf = {
	.rxmode = {
		.max_rx_pkt_len = 9128, /**< use this for jumbo frame */
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 1, /**< IP checksum offload disabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame	= 1, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 1, /**< CRC stripped by hardware */
		.mq_mode		= ETH_MQ_RX_RSS,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_hf = ETH_RSS_NONFRAG_IPV4_TCP | ETH_RSS_NONFRAG_IPV4_UDP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
	.fdir_conf = {
		.mode = RTE_FDIR_MODE_PERFECT, 
		.pballoc = RTE_FDIR_PBALLOC_256K,
		.mask = {
			.vlan_tci_mask = 0x0,
			.ipv4_mask = {
				.src_ip = 0xFFFFFFFF,
				.dst_ip = 0xFFFFFFFF,
			},
			.src_port_mask = 0,
			.dst_port_mask = 0xFFFF,
			.mac_addr_byte_mask = 0,
			.tunnel_type_mask = 0,
			.tunnel_id_mask = 0,
		},
		.drop_queue = 127,
		.flex_conf = {
			.nb_payloads = 0,
			.nb_flexmasks = 0,
		},
	},
};

/**
 * add_fdir_fules
 * Sets up flow director to direct incoming packets.
 */
int add_fdir_rules(uint8_t port_id)
{
    int ret;
	log_info("Adding FDIR rules.\n");

	// Check that flow director is supported.
	if (ret = rte_eth_dev_filter_supported(port_id, RTE_ETH_FILTER_FDIR)) {
		log_err("This device does not support Flow Director (Error %d).\n", ret);
		return -ENOTSUP;
	}

	// Flush any existing flow director rules.
	ret = rte_eth_dev_filter_ctrl(port_id, RTE_ETH_FILTER_FDIR, RTE_ETH_FILTER_FLUSH, NULL);
	if (ret < 0) {
		log_err("Could not flush FDIR entries.\n");
		return -1;
	}

	// Add flow director rules (currently added from static config file in cfg.c).
	ret = parse_cfg_fdir_rules(port_id);

	return ret;
}

/**
 * init_port
 * Sets up the ethernet port on a given port id.
 */
static void init_port(uint8_t port_id, struct eth_addr *mac_addr)
{
	int ret;
    int i;

	uint16_t nb_rx_q = CFG.num_cpus; 
	uint16_t nb_tx_q = CFG.num_cpus;
	struct rte_eth_conf *dev_conf; 

	dev_conf = &default_eth_conf;

	uint16_t nb_tx_desc = ETH_DEV_TX_QUEUE_SZ; //1024
	uint16_t nb_rx_desc = ETH_DEV_RX_QUEUE_SZ; //512
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf* txconf;
	struct rte_eth_rxconf* rxconf; 
	uint16_t mtu;
		
	if (dev_conf->rxmode.jumbo_frame) {
		dev_conf->rxmode.max_rx_pkt_len = 9000 + ETHER_HDR_LEN + ETHER_CRC_LEN;
	}
    if(rte_eal_process_type() == RTE_PROC_PRIMARY) {
        ret = rte_eth_dev_configure(port_id, nb_rx_q * CFG.num_process, nb_tx_q * CFG.num_process, dev_conf);
        if (ret < 0) {
			rte_exit(EXIT_FAILURE, "rte_eth_dev_configure:err=%d, port=%u\n",
			    			 ret, (unsigned) port_id);
		}
    }

    rte_eth_dev_info_get(port_id, &dev_info);
	txconf = &dev_info.default_txconf;  //FIXME: this should go here but causes TCP rx bug
		rxconf = &dev_info.default_rxconf;
				
		if (dev_conf->rxmode.jumbo_frame) {
			rte_eth_dev_set_mtu(port_id, 9000);	
			rte_eth_dev_get_mtu(port_id, &mtu);
			printf("Enable jumbo frames. MTU size is %d\n", mtu);
			//FIXME: rx_qinfo crashes with ixgbe DPDK driver (but works fine on AWS)
			//struct rte_eth_rxq_info rx_qinfo;
            //rte_eth_rx_queue_info_get(port_id, 0, &rx_qinfo);
			//rx_qinfo.scattered_rx = 1;
			txconf->txq_flags = 0; 
		}


		// initialize one queue per cpu
        if(rte_eal_process_type() == RTE_PROC_PRIMARY) {

		    for (i = 0; i < CFG.num_cpus * CFG.num_process; i++) {
                
		    	log_info("setting up TX and RX queues...\n");

                ret = rte_eth_tx_queue_setup(port_id, i, nb_tx_desc, rte_eth_dev_socket_id(port_id), txconf);
		    	if (ret < 0) {
		    		rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
		    				 ret, (unsigned) port_id);
		    	}

		    	rte_eth_rx_queue_setup(port_id, i, nb_rx_desc, rte_eth_dev_socket_id(port_id), rxconf, dpdk_pool);
		    	if (ret <0 ) {
		    		rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
		    				 ret, (unsigned) port_id);
		    	}
		    }
        }

		rte_eth_promiscuous_enable(port_id);

        ret = rte_eth_dev_start(port_id);
		if (ret < 0) {
			printf("ERROR starting device at port %d\n", port_id);
		}
		else {
			printf("started device at port %d\n", port_id);
		}
		

		struct rte_eth_link	link;
		rte_eth_link_get(port_id, &link);



	    if (!link.link_status) {
		    log_warn("eth:\tlink appears to be down, check connection.\n");
	    } else {
		    printf("eth:\tlink up - speed %u Mbps, %s\n",
			   (uint32_t) link.link_speed,
			   (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
			   ("full-duplex") : ("half-duplex"));
		    rte_eth_macaddr_get(port_id, mac_addr);
		    active_eth_port = port_id;
	    }
}


/**
 * init_ethdev - initializes an ethernet device
 *
 * Returns 0 if successful, otherwise fail.
 */
static int init_ethdev(void)
{
	int ret;

	// DPDK init for pci ethdev already done in dpdk_init()
	uint8_t port_id;
	uint8_t nb_ports;
	struct ether_addr mac_addr;

	nb_ports = rte_eth_dev_count();
	if (nb_ports == 0) rte_exit(EXIT_FAILURE, "No Ethernet ports - exiting\n");
	if (nb_ports > 1) printf("WARNING: only 1 ethernet port is used\n");
	if (nb_ports > RTE_MAX_ETHPORTS) nb_ports = RTE_MAX_ETHPORTS;
	
	for (port_id = 0; port_id < nb_ports; port_id++) {
		init_port(port_id, &mac_addr);
		log_info("Ethdev on port %d initialised.\n", port_id);
	    if(rte_eal_process_type() == RTE_PROC_PRIMARY) {	
		    ret = add_fdir_rules(port_id);

		    if (ret) {
		    	log_err("Adding FDIR rules failed. (Error %d)\n", ret);
		    } else {
		    	log_info("All FDIR rules added.\n");
		    }	
        }
	}
	
	struct eth_addr* macaddr = &mac_addr;
	CFG.mac = *macaddr;
	//percpu_get(eth_num_queues) = CFG.num_cpus; //NOTE: assume num tx queues == num rx queues
	percpu_get(eth_num_queues) = nb_ports;

    return 0;
}

/**
 * init_create_cpu - initializes a CPU
 * @cpu: the CPU number
 * @eth: the ethernet device to assign to this CPU
 *
 * Returns 0 if successful, otherwise fail.
 */
static int init_create_cpu(unsigned int cpu, int first)
{
	int ret = 0, i;

	if (!first)
		ret = cpu_init_one(cpu);

	if (ret) {
		log_err("init: unable to initialize CPU %d\n", cpu);
		return ret;
	}

	log_info("init: percpu phase %d\n", cpu);
	for (i = 0; init_tbl[i].name; i++)
		if (init_tbl[i].fcpu) {
			ret = init_tbl[i].fcpu();
			log_info("init: module %-10s on %d: %s \n", init_tbl[i].name, RTE_PER_LCORE(cpu_id), (ret ? "FAILURE" : "SUCCESS"));
			if (ret)
				panic("could not initialize IX\n");
		}


	log_info("init: CPU %d ready\n", cpu);
	printf("init:CPU %d ready\n", cpu);
	return 0;
}

static pthread_mutex_t spawn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spawn_cond = PTHREAD_COND_INITIALIZER;

struct spawn_req {
	void *arg;
	struct spawn_req *next;
};

static struct spawn_req *spawn_reqs;
extern void *pthread_entry(void *arg);

static void wait_for_spawn(void)
{
	struct spawn_req *req;
	void *arg;

	pthread_mutex_lock(&spawn_mutex);
	while (!spawn_reqs)
		pthread_cond_wait(&spawn_cond, &spawn_mutex);
	req = spawn_reqs;
	spawn_reqs = spawn_reqs->next;
	pthread_mutex_unlock(&spawn_mutex);

	arg = req->arg;
	free(req);

	log_info("init: user spawned cpu %d\n", RTE_PER_LCORE(cpu_id));
	//pthread_entry(arg);
	//pp_main(NULL);
}

int init_do_spawn(void *arg)
{
	struct spawn_req *req;

	pthread_mutex_lock(&spawn_mutex);
	req = malloc(sizeof(struct spawn_req));
	if (!req) {
		pthread_mutex_unlock(&spawn_mutex);
		return -ENOMEM;
	}

	req->next = spawn_reqs;
	req->arg = arg;
	spawn_reqs = req;
	pthread_cond_broadcast(&spawn_cond);
	pthread_mutex_unlock(&spawn_mutex);

	return 0;
}

static int init_fg_cpu(void)
{
	int fg_id, ret;
	int start;
	DEFINE_BITMAP(fg_bitmap, ETH_MAX_TOTAL_FG);

	start = RTE_PER_LCORE(cpu_nr);

	bitmap_init(fg_bitmap, ETH_MAX_TOTAL_FG, 0);



	for (fg_id = start; fg_id < nr_flow_groups; fg_id += CFG.num_cpus)
		bitmap_set(fg_bitmap, fg_id);

	eth_fg_assign_to_cpu(fg_bitmap, RTE_PER_LCORE(cpu_nr));
    

	for (fg_id = start; fg_id < nr_flow_groups; fg_id += CFG.num_cpus) {
		eth_fg_set_current(fgs[fg_id]);

		assert(fgs[fg_id]->cur_cpu == RTE_PER_LCORE(cpu_id));

		tcp_init(fgs[fg_id]);
		ret = tcp_api_init_fg();
		if (ret) {
			log_err("init: failed to initialize tcp_api \n");
			return ret;
		}

		timer_init_fg();
	}


	unset_current_fg();

	//FIXME: figure out flow group stuff, this is temp fix for fg_id == cpu_id (no migration)
	fg_id = percpu_get(cpu_id);
	//fg_id = outbound_fg_idx();
	fgs[fg_id] = malloc(sizeof(struct eth_fg));
	fgs[fg_id] = malloc(sizeof(struct eth_fg));
	memset(fgs[fg_id], 0, sizeof(struct eth_fg));
	eth_fg_init(fgs[fg_id], fg_id);
	eth_fg_init_cpu(fgs[fg_id]);
	fgs[fg_id]->cur_cpu = RTE_PER_LCORE(cpu_id);
	fgs[fg_id]->fg_id = fg_id;
	//fgs[fg_id]->eth = percpu_get(eth_rxqs[0])->dev;
	tcp_init(fgs[fg_id]);


	return 0;
}

static pthread_barrier_t start_barrier;
static volatile int started_cpus;

void *start_cpu(void *arg)
{
	log_info("start_cpu\n");
	int ret;
	unsigned int cpu_nr_ = (unsigned int)(unsigned long) arg;
	unsigned int cpu = CFG.cpu[cpu_nr_];

	ret = init_create_cpu(cpu, 0);
	if (ret) {
		log_err("init: failed to initialize CPU %d\n", cpu);
		exit(ret);
	}

	started_cpus++;

	/* percpu_get(cp_cmd) of the first CPU is initialized in init_hw. */

	RTE_PER_LCORE(cpu_nr) = cpu_nr_;
	percpu_get(cp_cmd) = &cp_shmem->command[started_cpus];
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_RUNNING;

	//pthread_barrier_wait(&start_barrier);
	rte_smp_mb(); 

	//log_info("skipping fg init for now.....\n");
	
	ret = init_fg_cpu();
	if (ret) {
		log_err("init: failed to initialize flow groups\n");
		exit(ret);
	}
	

	return NULL;
}


static int init_hw(void)
{
	// If we are not on the master lcore, we don't spawn new threads
	int master_id = rte_get_master_lcore();
	int lcore_id = rte_lcore_id();
	
	if (master_id != lcore_id)
		return 0;


	int i, ret = 0;
	pthread_t tid;
	int j;
	int fg_id;

	// will spawn per-cpu initialization sequence on CPU0
	ret = init_create_cpu(CFG.cpu[0], 1);
	if (ret) {
		log_err("init: failed to create CPU 0\n");
		return ret;
	}

	log_info("created cpu\n");

	RTE_PER_LCORE(cpu_nr) = 0;
	percpu_get(cp_cmd) = &cp_shmem->command[0];
	percpu_get(cp_cmd)->cpu_state = CP_CPU_STATE_RUNNING;

	for (i = 1; i < CFG.num_cpus; i++) {
		//ret = pthread_create(&tid, NULL, start_cpu, (void *)(unsigned long) i);
		log_info("rte_eal_remote_launch...start_cpu\n");
		ret = rte_eal_remote_launch(start_cpu, (void *)(unsigned long) i, i);		

		if (ret) {
			log_err("init: unable to create lthread\n");
			return -EAGAIN;
		}
		while (started_cpus != i)
			usleep(100);
	}


	/*
	fg_id = 0;
	for (i = 0; i < CFG.num_ethdev; i++) {
		struct ix_rte_eth_dev *eth = eth_dev[i];

		if (!eth->data->nb_rx_queues)
			continue;

		ret = eth_dev_start(eth); 
		if (ret) {
			log_err("init: failed to start eth%d\n", i);
			return ret;
		}

		for (j = 0; j < eth->data->nb_rx_fgs; j++) {
			eth_fg_init_cpu(&eth->data->rx_fgs[j]);
			fgs[fg_id] = &eth->data->rx_fgs[j];
			fgs[fg_id]->dev_idx = i;
			fgs[fg_id]->fg_id = fg_id;
			fg_id++;
		}
	}
	*/

	nr_flow_groups = fg_id;
	cp_shmem->nr_flow_groups = nr_flow_groups;

	mempool_init();


	if (CFG.num_cpus > 1) {
		//pthread_barrier_wait(&start_barrier);
		//rte_smp_mb();
		rte_eal_mp_wait_lcore();

	}

	log_info("skipping init_fg_cpu for now...\n");

	init_fg_cpu();
	if (ret) {
		log_err("init: failed to initialize flow groups\n");
		exit(ret);
	}

	log_info("init: barrier after al CPU initialization\n");

	return 0;
}


static int init_cfg(void)
{
	return cfg_init(init_argc, init_argv, &args_parsed);

}

static int init_parse_cpu(void)
{
	return cfg_parse_cpu(init_argc, init_argv, &args_parsed);

}

static int init_firstcpu(void)
{
	int ret;
	unsigned long msr_val;
	unsigned int value;
	int i;

	cpus_active = CFG.num_cpus;
	cp_shmem->nr_cpus = CFG.num_cpus;
	if (CFG.num_cpus > 1) {
		pthread_barrier_init(&start_barrier, NULL, CFG.num_cpus);
	}

	for (i = 0; i < CFG.num_cpus; i++)
		cp_shmem->cpu[i] = CFG.cpu[i];


	ret = cpu_init_one(CFG.cpu[0]);
	if (ret) {
		log_err("init: failed to initialize CPU 0\n");
		return ret;
	}
	// TODO: Need to figure out how to replace this calculation
	/*
	msr_val = rdmsr(MSR_RAPL_POWER_UNIT);
	
	value = (msr_val & ENERGY_UNIT_MASK) >> ENERGY_UNIT_OFFSET;
	energy_unit = 1.0 / (1 << value);
	*/

	return ret;
}

int main(int argc, char *argv[])
{
	int ret, i;

	init_argc = argc;
	init_argv = argv;

	log_info("init: starting IX\n");

//    if(strcmp(argv[1], "secondary") == 0) {
//        PROCESS_SHOULD_BE_SECONDARY = true;
//    }

	log_info("init: cpu phase\n");
	for (i = 0; init_tbl[i].name; i++)
		if (init_tbl[i].f) {
			ret = init_tbl[i].f();
			log_info("init: module %-10s %s\n", init_tbl[i].name, (ret ? "FAILURE" : "SUCCESS"));
			if (ret)
				panic("could not initialize IX\n");
	}

	
	if (argc > 1) { 
		ret = reflex_client_main(argc - args_parsed, &argv[args_parsed]);
    } else {
		    ret = reflex_server_main(argc - args_parsed, &argv[args_parsed]);
    }
	if (ret) {
		//log_err("init: failed to start echoserver\n");
		log_err("init: failed to start reflex server\n");
		return ret;
	}

	log_info("init done\n");
	return 0;
}


# ReFlex

ReFlex is a software-based system that provides remote access to Flash with performance nearly identical to local Flash access. ReFlex closely integrates network and storage processing in a dataplane  kernel to achieve low latency and high throughput at low resource requirements. ReFlex uses a novel I/O scheduler to enforce tail latency and throughput service-level objectives (SLOs) for multiple clients sharing a remote Flash device.

ReFlex is an extension of the [IX dataplace operating system](https://github.com/ix-project/ix). ReFlex uses Intel [DPDK](http://dpdk.org) for fast packet processing and Intel [SPDK](http://www.spdk.io) for high performance access to NVMe Flash. There are two implementations of ReFlex currently available: a kernel implementation and a userspace implementation. 

In the kernel implementation (available in the [master](https://github.com/stanford-mast/reflex/tree/master) branch of this repository), the ReFlex kernel runs as a guest OS in Linux and relies on the [Dune](https://github.com/project-dune/dune) kernel for memory management and direct access to hardware (e.g., NIC and NVMe Flash queues) from userspace applications. This is the original implementation of ReFlex, as presented in the [paper](https://web.stanford.edu/group/mast/cgi-bin/drupal/system/files/reflex_asplos17.pdf) published at ASPLOS'17. 

In the userspace implementation (available in the [userspace](https://github.com/stanford-mast/reflex/tree/userspace) branch), network and storage processing is implemented in userspace and ReFlex uses the standard `igb_uio` module to bind a network device to a DPDK-provided network device driver. The userspace version of ReFlex does not require the Dune kernel module to be loaded. This means the userspace version of ReFlex is simpler to deploy.  


## Requirements for userspace version of ReFlex

ReFlex requires a NVMe Flash device and a [network interface card supported by Intel DPDK](http://dpdk.org/doc/nics). We have tested ReFlex with the Intel 82599 10GbE NIC and the following NVMe SSDs: Samsung PM1725 and Intel P3600. We have also tested ReFlex on Amazon Web Services EC2 instances i3.4xlarge (see end of setup instructions below for AWS EC2 instance setup).

ReFlex has been successfully tested on Ubuntu 16.04 LTS with kernel 4.4.0.

**Note:** ReFlex provides an efficient *dataplane* for remote access to Flash. To deploy ReFlex in a datacenter cluster, ReFlex should be combined with a control plane to manage Flash resources across machines and optimize the allocation of Flash IOPS and capacity.  

## Setup Instructions

There is currently no binary distribution of ReFlex. You will therefore need to compile the project from source, as described below. 

1. Obtain ReFlex source code and fetch dependencies:

   ```
   git clone https://github.com/stanford-mast/reflex.git
   cd reflex
   git checkout userspace
   ./deps/fetch-deps.sh
   ```

2. Install library dependencies: 

   ```
   sudo apt-get install libconfig-dev libnuma-dev libpciaccess-dev libaio-dev libevent-dev g++-multilib
   ```

3. Build the dependecies:

   ```
   sudo chmod +r /boot/System.map-`uname -r`
   make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
   make -sj64 -C deps/dpdk
   make -sj64 -C deps/dpdk install T=x86_64-native-linuxapp-gcc DESTDIR=deps/dpdk/x86_64-native-linuxapp-gcc
   export REFLEX_HOME=`pwd`
   cd deps/spdk
   ./configure --with-dpdk=$REFLEX_HOME/deps/dpdk/x86_64-native-linuxapp-gcc
   make
   cd ../.. 	
   ```

4. Build ReFlex:

   ```
   make -sj64
   ```
5. Set up the environment:

   ```
   cp ix.conf.sample ix.conf
    # modify at least host_addr, gateway_addr, devices, and nvme_devices
  
   sudo modprobe -r ixgbe
   sudo modprobe -r nvme
   sudo modprobe uio
   sudo insmod deps/dpdk/build/kmod/igb_uio.ko
   sudo deps/dpdk/usertools/dpdk-devbind.py --bind=igb_uio 0000:06:00.0   # insert device PCI address here!!! 

   sudo deps/spdk/scripts/setup.sh
   
   sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
   ```
   
6. Precondition the SSD and derive the request cost model:

   It is necessary to precondition the SSD to acheive steady-state performance and reproducible results across runs. We recommend preconditioning the SSD with the following local Flash tests: write *sequentially* to the entire address space using 128KB requests, then write *randomly* to the device with 4KB requests until you see performance reach steady state. The random write test typically takes about 10 to 20 minutes, depending on the device model and capacity. 
   
   After preconditioning, you should derive the request cost model for your SSD:

   ```
   cp sample.devmodel nvme_devname.devmodel
    # update request costs and token limits to match your SSD performance 
    # see instructions in comments of file sample.devmodel
	# in ix.conf file, update nvme_device_model=nvme_devname.devmodel
   ```
   You may use any I/O load generation tool (e.g. [fio](https://github.com/axboe/fio)) for preconditioning and request calibration tests. Note that if you use a Linux-based tool, you will need to reload the nvme kernel module for these tests (remember to unload it before running the ReFlex server). 
  
   For your convenience, we provide an open-loop, local Flash load generator based on the SPDK perf example application [here](https://github.com/anakli/spdk_perf). We modified the SPDK perf example application to report read and write percentile latencies. We also made the load generator open-loop, so you can sweep throughput by specifying a target IOPS instead of queue depth. See setup instructions for ReFlex users in the repository's [README](https://github.com/anakli/spdk_perf/blob/master/README.md). 

### Instructions for setting up an AWS EC2 instance to run ReFlex

To run ReFlex on Amazon Web Servies EC2, you will need an instance type that supports [Enhanced Networking](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/enhanced-networking.html#enabling_enhanced_networking) (this means the instance has a DPDK-supported Enhanced Networking Adapter). The instance also needs to have NVMe Flash. Use an i3.4xlarge or larger instance with Ubuntu 16.04 Linux. Using a dedicated instance is strongly recommended for performance measurements.

You will need to setup a [Virtual Private Cloud (VPC)](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-vpc.html) for your instance(s) so that you can add Ethernet interfaces on the instance(s) running ReFlex. When you are ready to launch your ReFlex instance, in the "configure instance details" step, you need to add an extra network interface. This is required because you need to bind one interface to the userspace igb_uio driver for ReFlex/DPDK and you need one interface bound to the kernel NIC driver to SSH into your instance. In the "configure instance details" step, set the network for your instance to be your VPC network and use the subnet you created within your VPC. You can create the subnet from the VPC dashboard.

To connect to your instance, you should create an [Elastic IP address](http://docs.aws.amazon.com/AWSEC2/latest/UserGuide/elastic-ip-addresses-eip.html) and then associate it to the eth0 interface on your instance. Go to your VPC dashboard -> Elastic IPs -> Allocate new address. Next, associate this address to the eth0 interface on your ReFlex instance after you have obtained the interface ID and private IP address by hovering over the eth0 field in the description of your instance in the EC2 dashboard.

When you have successfully launched and connected to your instance, follow the ReFlex setup instructions (steps 1-6) above.


## Running ReFlex 

### 1. Run the ReFlex server:

   ```
   sudo ./dp/ix
   ```

   ReFlex runs one dataplane thread per CPU core. If you want to run multiple ReFlex threads (to support higher throughput), set the `cpu` list in ix.conf and add `fdir` rules to steer traffic identified by {dest IP, src IP, dest port} to a particular core.

#### Registering service level objectives (SLOs) for ReFlex tenants:

* A *tenant* is a logical abstraction for accounting for and enforcing SLOs. ReFlex supports two types of tenants: latency-critical (LC) and best-effort (BE) tenants. 
* The current implementation of ReFlex requires tenant SLOs to be specified statically (before running ReFlex) in `pp_accept()` in `apps/reflex_server.c`. Each port ReFlex listens on can be associated with a separate SLO. The tenant should communicate with ReFlex using the destination port that corresponds to the appropriate SLO. This is a temporary implementation until there is proper client API support for a tenant to dynamically register SLOs with ReFlex. 

 > As future work, a more elegant approach would be to i) implement a ReFlex control plane that listens on a dedicated admin port, ii) provide a client API for a tenant to register with ReFlex on this admin port and specify its SLO, and iii) provide a response from the ReFlex control plane to the tenant, indicating which port the tenant should use to communicate with the ReFlex data plane.

### 2. Run a ReFlex client:

There are several options for clients:

1. Run a Linux-based client that opens TCP connections to ReFlex and sends read/write reqs to logical blocks. 

	* This client option avoids the performance overheads of the filesystem  and block layers in the client machine. However, the client is still subject to any latency or throughput inefficiencies of the networking layer in the  Linux operating system.

	On the client machine, setup the `mutilate` load generator. We provide a modified version of `mutilate` which supports the ReFlex block protocol.

	```
	git clone https://github.com/anakli/mutilate.git
	cd mutilate
	sudo apt-get install scons libevent-dev gengetopt libzmq-dev
	scons
	```

	To achieve the best performance with a Linux client, we recommend tuning NIC settings and disabling CPU and PCIe link power management on the client. 
	
	Network setup tuning (recommended):

	```
	 # enable jumbo frames
	sudo ifconfig ethX mtu 9000
	 # tune interrupt coalescing 
	sudo ethtool -C ethX rx-usecs 50
	 # disable LRO/GRO offloads 
	sudo ethtool -K ethX lro off
	sudo ethtool -K ethX gro off
	```

	Disable power mangement (recommended):

   	```
	## to disable CPU power management, run:
	#!/bin/bash
	for i in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    	echo performance > $i
	done

	## to disable PCIe link power management:
	sudo vi /etc/default/grub 
     # modify existing line:
     # GRUB_CMDLINE_LINUX_DEFAULT="quiet splash pcie_aspm=force processor.max_cstate=1 idle=poll"
   	sudo grub-mkconfig -o /boot/grub/grub.cfg
   	sudo reboot
   	sudo su
   	echo performance > /sys/module/pcie_aspm/parameters/policy
   	```

	Run Linux client:

	```
	./mutilate -s IP:PORT --noload -r NUM_LOGICAL_BLOCK_ADDRESSES -K 8 -V 4096 --binary
	```

	This command is for an unloaded latency test; it launches a single client thread issuing requests with queue depth 1. Sample expected output:
	
	```
    #type       avg     std     min     5th    10th    90th    95th    99th     max
	read      115.2     8.7   101.8   107.3   108.1   125.8   127.6   129.1   351.3
	update      0.0     0.0     0.0     0.0     0.0     0.0     0.0     0.0     0.0
	op_q        1.0     0.0     1.0     1.0     1.0     1.1     1.1     1.1     1.0

	Total QPS = 8662.4 (519747 / 60.0s)
	```	
	
	Follow instructions in [mutilate README](https://github.com/anakli/mutilate/blob/master/README.md) to launch multiple client threads across multiple agent machines for high-throughput tests.


2. Run an IX-based client that opens TCP connections to ReFlex and sends read/write requests to logical blocks.

	* This client option avoids networking and storage (filesystem and block layer) overhead on the client machine. It requires the client machine to run IX.
   	
   Clone ReFlex source code (userspace branch) on client machine and follow steps 1 to 6 in the setup instructions in userspace branch README. Comment out `nvme_devices` in ix.conf. 
   
   There are currently two modes (open-loop and closed-loop testing) available on the IX-based client.
   
   To run the open-loop ReFlex client:  

   ```
   # example: sudo ./dp/ix -s 10.79.6.130 -p 1234 -w seq -T 1 -i 1000 -r 100 -S 1 -R 1024 -P 0 
   ```

   To run the closed-loop ReFlex client: 
   ```
    # example: sudo ./dp/ix -s 10.79.6.130 -p 1234 -w seq -T 1 -i 1000 -r 100 -S 0 -R 1024 -P 0 -d 1 -t 5  
   ```
   
   More descriptions for the command line options can be found by running
   ```
   sudo ./dp/ix -h
   
   Usage: 
   sudo ./dp/ix
   to run ReFlex server, no parameters required
   to run ReFlex client, set the following options
   -s  server IP address
   -p  server port number
   -w  workload type [seq/rand] (default=rand)
   -T  number of threads (default=1)
   -i  target IOPS for open-loop test (default=50000)
   -r  percentage of read requests (default=100)
   -S  sweep multiple target IOPS for open-loop test (default=1)
   -R  request size in bytes (default=1024)
   -P  precondition (default=0)
   -d  queue depth for closed-loop test (default=0)
   -t  execution time for closed-loop test (default=0)
   ```



   Sample output:

   ```
   RqIOPS:	 IOPS: 	 Avg:  	 10th: 	 20th: 	 30th: 	 40th: 	 50th: 	 60th: 	 70th: 	 80th: 	 90th: 	 95th: 	 99th: 	 max:  	 missed:
   1000   	 1000  	 98    	 92    	 93    	 94    	 94    	 95    	 96    	 110   	 111   	 112   	 113   	 117   	 149   	 0
   10000  	 10000 	 98    	 92    	 92    	 93    	 94    	 94    	 95    	 110   	 111   	 112   	 112   	 113   	 172   	 9
   50000  	 49999 	 100   	 92    	 93    	 94    	 95    	 95    	 96    	 111   	 111   	 113   	 114   	 134   	 218   	 682
   100000 	 99999 	 102   	 93    	 94    	 95    	 96    	 97    	 104   	 112   	 113   	 116   	 122   	 155   	 282   	 44385
   150000 	 150001	 105   	 94    	 95    	 96    	 98    	 100   	 109   	 113   	 115   	 120   	 132   	 168   	 357   	 303583
   200000 	 199997	 109   	 96    	 97    	 99    	 101   	 105   	 112   	 115   	 118   	 127   	 145   	 178   	 440   	 923940
   ```

   For high-throughput tests, increase the number of IX client threads (and CPU cores configured in client ix.conf). You may also want to run multiple ReFlex threads on the server.


3.  Run a legacy client application using the ReFlex remote block device driver.

	* This client option is provided to support legacy applications. ReFlex exposes a standard Linux remote block device interface to the client (which appears to the client as a local block device). The client can mount a filesystem on the block device. With this approach, the client is subject to overheads in the Linux filesystem, block storage layer and network stack.

    On the client, change into the reflex_nbd directory and type make. Be sure that the remote ReFlex server is running and that it is ping-able from the client. Load the reflex.ko module, type dmesg to check whether the driver was successfully loaded. To reproduce the fio results from the paper do the following.
    ```
    cd reflex_nbd
    make

	sudo modprobe ixgbe
	sudo modprobe nvme
	 # make sure you have started reflex_server on the server machine and can ping it
    
	sudo insmod reflex.ko
    sudo mkfs.ext4 /dev/reflex0
    sudo mkdir /mnt/reflex
    sudo mount /dev/reflex0 /mnt/reflex
	sudo chmod a+rw /mnt/reflex
    touch /mnt/reflex/fio
    sudo apt install fio
    for i in 1 2 4 8 16 32 ; do BLKSIZE=4k DEPTH=$i fio randread_remote.fio; done
    ```
## Reference

Please refer to the ReFlex [paper](https://web.stanford.edu/group/mast/cgi-bin/drupal/system/files/reflex_asplos17.pdf):

Ana Klimovic, Heiner Litz, Christos Kozyrakis
ReFlex: Remote Flash == Local Flash
in the 22nd International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS'22), 2017

## License and Copyright

ReFlex is free software. You can redistribute and/or modify the code under the terms of the BSD License. See [LICENSE](https://github.com/stanford-mast/reflex/blob/master/LICENSE). 

The original ReFlex code was written collaboratively by Ana Klimovic and Heiner Litz at Stanford University. Per Stanford University policy, the copyright of the original code remains with the Board of Trustees of Leland Stanford Junior University.

## Future work

The userspace implementation of ReFlex is for the ReFlex server. We are working on a shared userspace library based on ReFlex/IX which will allow client applications to submit requests to a remote ReFlex server using the efficient DPDK-based TCP network processing in ReFlex/IX. 

The current implementation of ReFlex would also benefit from the following:

* Client API support for tenants to dynamically register SLOs. 
* Support for multiple NVMe device management by a single ReFlex server.
* Support for light weight network protocol such as UDP (currently use TCP/IP).


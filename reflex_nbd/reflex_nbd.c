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
 * ReFlex Block Device Driver
 * Access remote NVME flash via a block device
 *
 * This driver is based on Linux's nbd_dev
 * author: Heiner Litz
 *  
 */

#include <linux/major.h>

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <net/sock.h>
#include <linux/kthread.h>

#include "reflex_nbd.h"

#define REFLEX_MAGIC 0x68797548
#define DEFAULT_TIMEOUT 10000

#define REFLEX_MAJOR MISC_MAJOR
#define REFLEX_SIZEBYTES 0x1749a956000

#ifdef NDEBUG
#define dprintk(flags, fmt...)
#else /* NDEBUG */
#define dprintk(flags, fmt...) do {					\
		if (debugflags & (flags)) printk(KERN_DEBUG fmt);	\
	} while (0)
#define DBG_IOCTL       0x0004
#define DBG_INIT        0x0010
#define DBG_EXIT        0x0020
#define DBG_BLKDEV      0x0100
#define DBG_RX          0x0200
#define DBG_TX          0x0400
static unsigned int debugflags;
#endif /* NDEBUG */

static unsigned int reflex_devs_max = 1;
static struct reflex_device *reflex_dev;
static int max_part = 15;
static char *dest_addr = "10.79.6.130";
static int hw_queue_depth = 4096;
static int submit_queues;
static int home_node = NUMA_NO_NODE;
//FIXME: max cores/hw queues

#ifndef NDEBUG

static const char *nbdcmd_to_ascii(int cmd)
{
	switch (cmd) {
	case  REFLEX_CMD_READ: return "read";
	case REFLEX_CMD_WRITE: return "write";
	case REFLEX_CMD_FLUSH: return "flush";
	case  REFLEX_CMD_TRIM: return "trim/discard";
	}
	return "invalid";
}
#endif /* NDEBUG */

/* parse inet addr */
unsigned int inet_addr(char *str)
{
	int a, b, c, d;
	char arr[4];
	sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
	arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
	return *(unsigned int *)arr;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

static void reflex_end_request(struct reflex_queue *fq, struct request *req)
{
	int error = req->errors ? -EIO : 0;
	unsigned long flags = 0;

	dprintk(DBG_BLKDEV, "%s: request %p: %s\n", req->rq_disk->disk_name,
		req, error ? "failed" : "done");
	
	BUG_ON(error);
	blk_mq_end_request(req, error);
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct socket *sock, int send, void *buf, int size,
		     int msg_flags)
{
	//struct socket *sock = nbd->sock;
	int result;
	struct msghdr msg;
	struct kvec iov;
	//sigset_t blocked, oldset;
	unsigned long pflags = current->flags;
	mm_segment_t oldfs;

	if (unlikely(!sock)) {
		return -EINVAL;
	}

	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	//siginitsetinv(&blocked, sigmask(SIGKILL));
	//sigprocmask(SIG_SETMASK, &blocked, &oldset);

	current->flags |= PF_MEMALLOC;

	do {
		sock->sk->sk_allocation = GFP_NOIO | __GFP_MEMALLOC;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send) {
			result = kernel_sendmsg(sock, &msg, &iov, 1, size);
		} else {
			result = kernel_recvmsg(sock, &msg, &iov, 1, size,
						msg.msg_flags);
			if (kthread_should_stop()) {
				return -EIO;
			}
		}

		if (result > 0) {
			size -= result;
			buf += result;
		}
		  
	} while (size > 0);
	
	tsk_restore_flags(current, pflags, PF_MEMALLOC);
	//sigprocmask(SIG_SETMASK, &oldset, NULL);
	
	
	return result;
}

static inline int sock_send_bvec(struct socket *sock, struct bio_vec *bvec,
				 int flags, struct reflex_queue *fq)
{
	int result;
	unsigned long irq_flags;

	void *kaddr = kmap(bvec->bv_page);
	
	result = sock_xmit(sock, 1, kaddr + bvec->bv_offset,
			   bvec->bv_len, flags);
	kunmap(kaddr);
	
	return result;
}

/* always call with the tx_lock held */
static int reflex_send_req(struct reflex_queue *fq, struct request *req)
{
	struct socket *sock = fq->sock;
	int result, flags;
	binary_header_blk_t header;
	struct req_iterator iter;
	struct bio_vec bvec;
	int sent_reflex_reqs = 0;
	int reflex_reqs = blk_rq_bytes(req) >> 9;
	int cmd_type = req->cmd_type; //make cmd_type thread local
	struct bio *bio;
	int num_bio = 0;
	
	if(unlikely((req->cmd_type != REFLEX_CMD_WRITE) && (req->cmd_type != REFLEX_CMD_READ))) {
		printk("Unsupported command received %s\n", nbdcmd_to_ascii(req->cmd_type));
		goto error_out;
	}
	
	header.lba = blk_rq_pos(req);
	//FIXME: Shift value must correspond with reflex server sector size
	header.lba_count = blk_rq_bytes(req) >> 9; 
	header.magic = sizeof(binary_header_blk_t);
	header.opcode = (req->cmd_type == REFLEX_CMD_WRITE) ? CMD_SET : CMD_GET;
	BUG_ON((req->cmd_type != REFLEX_CMD_WRITE) && (req->cmd_type != REFLEX_CMD_READ));
	header.req_handle = req;

	result = sock_xmit(sock, 1, &header, sizeof(binary_header_blk_t),
			   (cmd_type == REFLEX_CMD_WRITE) ? MSG_MORE : 0); 
	BUG_ON(result != sizeof(binary_header_blk_t));
	bio = req->bio;
	while (bio) {
		struct bio *next = bio->bi_next;
		struct bvec_iter iter;
		struct bio_vec bvec;

		if (cmd_type == REFLEX_CMD_WRITE) {
			bio_for_each_segment(bvec, bio, iter) {
				bool is_last = !next && bio_iter_last(bvec, iter);
				int flags = is_last ? 0 : MSG_MORE;

				result = sock_send_bvec(sock, &bvec, flags, fq);
				if (result <= 0) {
					printk(KERN_EMERG "FATAL cound not send\n");
					BUG();
				}
				/*
				 * The completion might already have come in,
				 * so break for the last one instead of letting
				 * the iterator do it. This prevents use-after-free
				 * of the bio.
				 */
				if (is_last)
					break;
			}
		}
		bio = next;
	}
	return sent_reflex_reqs;
	
error_out:
	return -EIO;
}

static inline int sock_recv_bvec(struct socket *sock, struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);

	result = sock_xmit(sock, 0, kaddr + bvec->bv_offset, bvec->bv_len,
			   MSG_WAITALL);

	kunmap(bvec->bv_page);

	return result;
}

static int reflex_read_stat(struct reflex_queue *fq)
{
	int result = 0;
	struct request *req;
	int i;
	binary_header_blk_t header;
	void* handle;
	int recv_bytes = 0;
	int recv_reqs = 0;

	while (1) {
		result = sock_xmit(fq->sock, 0, &header, sizeof(binary_header_blk_t), MSG_WAITALL);

		if (result == -EIO) {
			BUG();
			return result;
		}

		BUG_ON(result < 0);
		BUG_ON(header.magic != sizeof(binary_header_blk_t));

		req = header.req_handle;
	
		if (req->cmd_type == REFLEX_CMD_READ) {
			struct bio_vec bvec;
			struct req_iterator iter;
			struct request *req = header.req_handle;
			
			rq_for_each_segment(bvec, req, iter) {
				result = sock_recv_bvec(fq->sock, &bvec);
				if (result <= 0) {
					printk(KERN_EMERG "FATAL cound not receive\n");
					BUG();
				}
			}
		}
		reflex_end_request(fq, req);
	}

	return recv_reqs;
}

static int reflex_thread_recv(void *arg)
{
	struct request *req;
	int ret;
	struct reflex_queue *fq = (struct reflex_queue *)arg;
	
	BUG_ON(fq->reflex_dev->magic != REFLEX_MAGIC);

	while (!kthread_should_stop()) {
		reflex_read_stat(fq);
	}

	return 0;
}

static void reflex_handle_cmd(struct reflex_queue *fq, struct reflex_cmd *cmd)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	int num_reflex_reqs;
	ktime_t ktime;
	int ret;
	
	if (req->cmd_type != REQ_TYPE_FS) {
		printk(KERN_WARNING "---starnge cm type %i\n", req->cmd_type);
		goto error_out;

	}

	BUG_ON(req->cmd_flags & REQ_FLUSH);
	BUG_ON(req->cmd_flags & REQ_FLUSH_SEQ);
	BUG_ON(req->cmd_flags & REQ_DISCARD);
	BUG_ON(req->cmd_flags & REQ_SOFTBARRIER);
	BUG_ON(req->cmd_flags & REQ_STARTED);
	
	req->cmd_type = REFLEX_CMD_READ;
	if (rq_data_dir(req) == WRITE) {
		if ((req->cmd_flags & REQ_DISCARD)) {
			req->cmd_type = REFLEX_CMD_TRIM;
		} else
			req->cmd_type = REFLEX_CMD_WRITE;
	}

	if (req->cmd_flags & REQ_FLUSH) {
		BUG_ON(unlikely(blk_rq_sectors(req)));
		req->cmd_type = REFLEX_CMD_FLUSH;
	}

	req->errors = 0;

	if (unlikely(!fq->sock)) {
		goto error_out;
	}

	mutex_lock(&fq->tx_lock);
	num_reflex_reqs = reflex_send_req(fq, req);
	mutex_unlock(&fq->tx_lock);
	
	return;

error_out:
	req->errors++;
	reflex_end_request(fq, req);
	BUG();
}

static const struct block_device_operations reflex_fops =
{
	.owner =	THIS_MODULE,
	//	.ioctl =	reflex_ioctl,
};

static int reflex_queue_rq(struct blk_mq_hw_ctx *hctx,
			    const struct blk_mq_queue_data *bd)
{
	struct reflex_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);
	struct reflex_queue *fq = hctx->driver_data;
		
	blk_mq_start_request(bd->rq);
	reflex_handle_cmd(fq, cmd);
	  
	return BLK_MQ_RQ_QUEUE_OK;	
}

static int reflex_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			     unsigned int index)
{
	struct reflex_device *reflex_dev = data;
	struct reflex_queue *fq = &reflex_dev->queues[index];
	int sock_flags = 0;
	struct sockaddr_in sockaddr;
	int ret = 0;
	int US_POLL_DELAY = 1;
	int len = sizeof(int);
	struct timeval tv;
	
	BUG_ON(!reflex_dev);
	BUG_ON(!fq);

	fq->index = index;
	fq->reflex_dev = reflex_dev;
	
	mutex_init(&fq->tx_lock);
	hctx->driver_data = fq;
	fq->reflex_reqs = kzalloc(sizeof(long) * hw_queue_depth, GFP_KERNEL);

	/* Connect to reflex server */
	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &fq->sock);
	if(ret) {
		printk(KERN_WARNING "Could not create socket\n");
		goto out;
	}

	tv.tv_sec = 0;  /* 100 ms Timeout */
	tv.tv_usec = 100000;  
	kernel_setsockopt(fq->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
			  sizeof(struct timeval));
	
	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = inet_addr(dest_addr);
	sockaddr.sin_port = htons(1234);

	printk(KERN_EMERG "Connecting to IP: %s\n", dest_addr);
	
	ret = kernel_connect(fq->sock, (struct sockaddr *)&sockaddr,
			     sizeof(struct sockaddr_in), sock_flags); 
	if(ret) {
		printk(KERN_WARNING "Could not connect socket. Server not running?\n");
		goto out_sock;
	}

	fq->recvthread = kthread_create(reflex_thread_recv, fq, "Recv %s %i",
					reflex_dev->disk->disk_name, index);
	if (IS_ERR(fq->recvthread)) {
		printk(KERN_WARNING "error creating recv thread\n");
		goto out_sock;
	}
	//kthread_bind(fq->recvthread, index + 6);//smp_processor_id());
	wake_up_process(fq->recvthread);
	
	return 0;
	
out_sock:
	sock_release(fq->sock);
out:
	kfree(fq->reflex_reqs);
	return -1;
}


static struct blk_mq_ops reflex_mq_ops = {
	.queue_rq       = reflex_queue_rq,
	.map_queue      = blk_mq_map_queue,
	.init_hctx	= reflex_init_hctx,
//	.complete	= reflex_softirq_done_fn,
};


static int setup_queues(struct reflex_device *dev)
{
	dev->queues = kzalloc(submit_queues * sizeof(struct reflex_queue),
			      GFP_KERNEL);
	if (!dev->queues)
		return -ENOMEM;

	dev->nr_queues = 0;
	
	return 0;
}

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static int __init reflex_init(void)
{
	int err = -ENOMEM;
	static int i = 0;
	int e;
	int part_shift;
	int ret;
	struct gendisk *disk;
	
	submit_queues = 24;//nr_online_nodes;
	
	if (max_part < 0) {
		printk(KERN_ERR "reflex: max_part must be >= 0\n");
		return -EINVAL;
	}

	part_shift = 0;
	if (max_part > 0) {
		part_shift = fls(max_part);

		/*
		 * Adjust max_part according to part_shift as it is exported
		 * to user space so that user can know the max number of
		 * partition kernel should be able to manage.
		 *
		 * Note that -1 is required because partition 0 is reserved
		 * for the whole disk.
		 */
		max_part = (1UL << part_shift) - 1;
	}

	if ((1UL << part_shift) > DISK_MAX_PARTS)
		return -EINVAL;

	if (reflex_devs_max > 1UL << (MINORBITS - part_shift))
		return -EINVAL;

	reflex_dev = kcalloc(reflex_devs_max, sizeof(*reflex_dev), GFP_KERNEL);
	if (!reflex_dev)
		return -ENOMEM;

	setup_queues(reflex_dev);
	
	reflex_dev->tag_set.ops = &reflex_mq_ops;
	reflex_dev->tag_set.nr_hw_queues = submit_queues;
	reflex_dev->tag_set.queue_depth = hw_queue_depth;
	reflex_dev->tag_set.numa_node = 0;
	reflex_dev->tag_set.cmd_size	= sizeof(struct reflex_cmd);
	reflex_dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	reflex_dev->tag_set.driver_data = reflex_dev;
	reflex_dev->magic = REFLEX_MAGIC;
	reflex_dev->flags = 0;
	reflex_dev->blksize = 512;
	reflex_dev->bytesize = REFLEX_SIZEBYTES;

	
		
	err = blk_mq_alloc_tag_set(&reflex_dev->tag_set);
	if (err)
		goto out_cleanup_queues;
	
	reflex_dev->q = blk_mq_init_queue(&reflex_dev->tag_set);
	if (IS_ERR(reflex_dev->q)) {
		err = -ENOMEM;
		goto out_cleanup_tags;
	}
	reflex_dev->q->queuedata = reflex_dev;

	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, reflex_dev->q);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, reflex_dev->q);


	blk_queue_logical_block_size(reflex_dev->q, 512);
	blk_queue_physical_block_size(reflex_dev->q, 512);
        blk_queue_max_hw_sectors(reflex_dev->q, 64);
	blk_queue_max_segments(reflex_dev->q, 8);
	blk_queue_max_integrity_segments(reflex_dev->q, 1);
	blk_queue_max_discard_sectors(reflex_dev->q, 0xffffffff);
		
	disk = reflex_dev->disk = alloc_disk_node(1, home_node);
	if (!disk) {
		ret = -ENOMEM;
		//FIXME do proper goto
		goto out;
	}
	
	disk->major = REFLEX_MAJOR;
	disk->first_minor = i;
	disk->fops = &reflex_fops;
	disk->private_data = reflex_dev;
	disk->queue = reflex_dev->q;

	/* ReFlex is not a rotational device */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, reflex_dev->q);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, reflex_dev->q);

	sprintf(disk->disk_name, "reflex%d", i);
	printk(KERN_WARNING "ReFlex: register device\n");
	if (register_blkdev(REFLEX_MAJOR, "reflex")) {
		err = -EIO;
		goto out;
	}

	printk(KERN_INFO "ReFlex: registered device at major %d\n", REFLEX_MAJOR);

	set_capacity(disk, 0);
	add_disk(disk);		
	set_capacity(disk, REFLEX_SIZEBYTES >> 9);
	i++;
	return 0;

out_sock:
	//FIXME release all socks
//	sock_release(reflex_dev->sock); 
out:
	unregister_blkdev(REFLEX_MAJOR, "reflex");

	while (i--) {
		blk_cleanup_queue(reflex_dev->disk->queue);
		put_disk(reflex_dev->disk);
	}
	kfree(reflex_dev);
	return err;

	//FIXME: handle errors
out_cleanup_queues:
	return err;
out_cleanup_tags:
	return err;
}

static void __exit reflex_cleanup(void)
{
	int i;
	
	for (i = 0; i < submit_queues; i++) {
		kthread_stop((&reflex_dev->queues[i])->recvthread);
		kernel_sock_shutdown((&reflex_dev->queues[i])->sock, SHUT_RDWR);
	}
	for (i = 0; i < reflex_devs_max; i++) {
		struct gendisk *disk = reflex_dev->disk;
		reflex_dev->magic = 0;
		if (disk) {
			del_gendisk(disk);
			blk_cleanup_queue(disk->queue);
			put_disk(disk);
		}
	}
	unregister_blkdev(REFLEX_MAJOR, "reflex");
	kfree(reflex_dev);
	printk(KERN_WARNING "ReFlex: unregistered device at major %d\n", REFLEX_MAJOR);
}

module_init(reflex_init);
module_exit(reflex_cleanup);

module_param(hw_queue_depth, int, S_IRUGO);
MODULE_PARM_DESC(hw_queue_depth, "Queue depth for each hardware queue. Default: 64");

module_param(submit_queues, int, S_IRUGO);
MODULE_PARM_DESC(submit_queues, "Number of submission queues");

module_param(home_node, int, S_IRUGO);
MODULE_PARM_DESC(home_node, "Home node for the device");

module_param(dest_addr, charp, S_IRUGO);
MODULE_PARM_DESC(dest_addr, "Reflex Server IP");

MODULE_DESCRIPTION("Reflex Network Block Device");
MODULE_LICENSE("GPL");

module_param(reflex_devs_max, int, 0444);
MODULE_PARM_DESC(nbds_max, "number of network block devices to initialize (default: 16)");
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "number of partitions per device (default: 0)");
#ifndef NDEBUG
module_param(debugflags, int, 0644);
MODULE_PARM_DESC(debugflags, "flags for controlling debug output");
#endif

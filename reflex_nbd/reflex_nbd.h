/*
 * 1999 Copyright (C) Pavel Machek, pavel@ucw.cz. This code is GPL.
 * 1999/11/04 Copyright (C) 1999 VMware, Inc. (Regis "HPReg" Duchesne)
 *            Made nbd_end_request() use the io_request_lock
 * 2001 Copyright (C) Steven Whitehouse
 *            New nbd_end_request() for compatibility with new linux block
 *            layer code.
 * 2003/06/24 Louis D. Langholtz <ldl@aros.net>
 *            Removed unneeded blksize_bits field from nbd_device struct.
 *            Cleanup PARANOIA usage & code.
 * 2004/02/19 Paul Clements
 *            Removed PARANOIA, plus various cleanup and comments
 */
#ifndef LINUX_REFLEX_H
#define LINUX_REFLEX_H

#include <linux/mutex.h>

#include "../apps/reflex.h"

enum {
	REFLEX_CMD_READ,
	REFLEX_CMD_WRITE,
	REFLEX_CMD_TRIM,
	REFLEX_CMD_FLUSH,
};

struct request;

struct reflex_cmd {
	struct request *rq;
//	struct reflex_queue *fq;
};

struct reflex_queue {
	int index;
	long *reflex_reqs;
	struct mutex tx_lock;
	struct reflex_device *reflex_dev;
	struct socket *sock;
	struct task_struct *recvthread;
};

struct reflex_device {
        int flags;
	int magic;

	struct gendisk *disk;
	int blksize;
	u64 bytesize;

	//blk-mq stuff
	struct request_queue *q;
	struct blk_mq_tag_set tag_set;
	
	struct reflex_queue *queues;
	unsigned int nr_queues;
	char disk_name[DISK_NAME_LEN];

};

/*
 * Memcached protocol support.
 * This part needs to be identical with the server definitions.
 */

#define CMD_GET  0x00
#define CMD_SET  0x01
#define CMD_SET_NO_ACK  0x02
//#define CMD_SASL 0x21
 
#define RESP_OK 0x00
#define RESP_EINVAL 0x04
//#define RESP_SASL_ERR 0x20

#define REQ_PKT 0x80
#define RESP_PKT 0x81
#define MAX_EXTRA_LEN 8
#define MAX_KEY_LEN 8

#endif

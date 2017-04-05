
enum msg_type {
	PUT,
	GET,
	PUT_ACK,
	GET_RESP,
};

struct msg_header {
	void* addr;
	int cmd;
	size_t len;
	int tag;
};

/*
 * ReFlex protocol support 
 */

#define CMD_GET  0x00
#define CMD_SET  0x01
#define CMD_SET_NO_ACK  0x02
 
#define RESP_OK 0x00
#define RESP_EINVAL 0x04

#define REQ_PKT 0x80
#define RESP_PKT 0x81
#define MAX_EXTRA_LEN 8
#define MAX_KEY_LEN 8

typedef struct __attribute__ ((__packed__)) {
  uint16_t magic;
  uint16_t opcode;
  void *req_handle;
  unsigned long lba;
  unsigned int lba_count;
} binary_header_blk_t;



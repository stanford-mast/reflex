#include <ix/list.h>
struct util {
    unsigned long num_req; //incremented when a request is fully recieved; reset every second
    //typedef unsigned long size_t
    size_t rxbytes;        //updated in eth_input_process; reset every second
    size_t txbytes;        //updated in ip_send_one; reset every second
    struct list_node link;
};

extern struct util *util_per_sec;

extern struct list_head *util_list; 
extern unsigned long start_time;




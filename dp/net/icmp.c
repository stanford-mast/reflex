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
 * icmp.c - Internet Control Message Protocol support
 *
 * See RFC 792 for more details.
 */

#include <sys/socket.h>
#include <rte_config.h>
#include <rte_mbuf.h>

#include <ix/stddef.h>
#include <ix/errno.h>
#include <ix/log.h>
#include <ix/timer.h>
#include <ix/cfg.h>

#include <asm/chksum.h>

#include <net/ethernet.h>
#include <net/ip.h>
#include <net/icmp.h>

#include "net.h"

static int icmp_reflect(struct eth_fg *cur_fg, struct rte_mbuf *pkt, struct icmp_hdr *hdr, int len)
{
	struct eth_hdr *ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
	struct ip_hdr *iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
	int ret;

	
	ethhdr->dhost = ethhdr->shost;
	ethhdr->shost = CFG.mac;

	/* FIXME: check for invalid (e.g. multicast) src addr */
	iphdr->dst_addr = iphdr->src_addr;
	iphdr->src_addr.addr = hton32(CFG.host_addr.addr);

	hdr->chksum = 0;
	hdr->chksum = chksum_internet((void *) hdr, len);

	pkt->ol_flags = 0;
	pkt->pkt_len = rte_pktmbuf_pkt_len(pkt);
	pkt->data_len = rte_pktmbuf_pkt_len(pkt);

	ret = rte_eth_tx_burst(0, 0, &pkt, 1);

	if (unlikely(ret < 1)) {
		printf("Warning: could not send ICMP reply\n");
		rte_pktmbuf_free(pkt);
		return -EIO;
	}

	return 0;
}

/*
 * icmp_input - handles an input ICMP packet
 * @pkt: the packet
 * @hdr: the ICMP header
 */
void icmp_input(struct eth_fg *cur_fg, struct rte_mbuf *pkt, struct icmp_hdr *hdr, int len)
{
	if (len < ICMP_MINLEN)
		goto out;
	if (chksum_internet((void *) hdr, len))
		goto out;

	log_info("icmp: got request type %d, code %d\n",
		  hdr->type, hdr->code);

	switch (hdr->type) {
	case ICMP_ECHO:
		hdr->type = ICMP_ECHOREPLY;
		icmp_reflect(cur_fg, pkt, hdr, len);
		break;
	case ICMP_ECHOREPLY: {
		uint16_t *seq;
		uint64_t *icmptimestamp;
		uint64_t time;

		seq = mbuf_nextd_off(hdr, uint16_t *, sizeof(struct icmp_hdr) + 2);
		icmptimestamp = mbuf_nextd_off(hdr, uint64_t *, sizeof(struct icmp_hdr) + 4);

		time = (rdtsc() - *icmptimestamp) / cycles_per_us;

		log_info("icmp: echo reply: %d bytes: icmp_req=%d time=%lld us\n",
			 len, ntoh16(*seq), time);
		goto out;
	}
	default:
		goto out;
	}

	return;

out:
	rte_pktmbuf_free(pkt);
}

int icmp_echo(struct eth_fg *cur_fg, struct ip_addr *dest, uint16_t id, uint16_t seq, uint64_t timestamp)
{
	int ret;
	struct rte_mbuf *pkt;
	struct eth_hdr *ethhdr;
	struct ip_hdr *iphdr;
	struct icmp_pkt *icmppkt;
	uint64_t *icmptimestamp;
	uint16_t len;

	pkt = mbuf_alloc_local();
	if (unlikely(!(pkt)))
		return -ENOMEM;

	ethhdr = mbuf_mtod(pkt, struct eth_hdr *);
	iphdr = mbuf_nextd(ethhdr, struct ip_hdr *);
	icmppkt = mbuf_nextd(iphdr, struct icmp_pkt *);
	icmptimestamp = mbuf_nextd_off(icmppkt, uint64_t *, sizeof(struct icmp_hdr) + 4);

	len = sizeof(struct icmp_hdr) + 4 + sizeof(uint64_t);

	iphdr->header_len = sizeof(struct ip_hdr) / 4;
	iphdr->version = 4;
	iphdr->tos = 0;
	iphdr->len = hton16(sizeof(struct ip_hdr) + len);
	iphdr->id = 0;
	iphdr->off = 0;
	iphdr->ttl = 64;
	iphdr->chksum = 0;
	iphdr->proto = IPPROTO_ICMP;
	iphdr->src_addr.addr = hton32(CFG.host_addr.addr);
	iphdr->dst_addr.addr = hton32(dest->addr);
	iphdr->chksum = chksum_internet((void *) iphdr, sizeof(struct ip_hdr));

	icmppkt->hdr.type = ICMP_ECHO;
	icmppkt->hdr.code = ICMP_ECHOREPLY;
	icmppkt->hdr.chksum = 0;
	icmppkt->icmp_id = hton16(id);
	icmppkt->icmp_seq = hton16(seq);
	*icmptimestamp = timestamp;
	icmppkt->hdr.chksum = chksum_internet((void *) icmppkt, len);

	pkt->ol_flags = 0;

	/* FIXME -- unclear if fg is/should be set */
	ret = ip_send_one(cur_fg, dest, pkt, sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + len);

	if (unlikely(ret)) {
		mbuf_free(pkt);
		return -EIO;
	}

	return 0;
}

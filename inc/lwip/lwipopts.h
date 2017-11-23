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

#define	LWIP_STATS	0
#define	LWIP_TCP	1
#define	NO_SYS		1
#define LWIP_RAW	0
#define LWIP_UDP	0
#define IP_REASSEMBLY	0
#define IP_FRAG		0
#define LWIP_NETCONN	0

#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1

//#define	LWIP_DEBUG		LWIP_DBG_OFF
#undef LWIP_DEBUG
#define	TCP_CWND_DEBUG		LWIP_DBG_OFF
#define	TCP_DEBUG		LWIP_DBG_OFF
#define	TCP_FR_DEBUG		LWIP_DBG_OFF
#define	TCP_INPUT_DEBUG		LWIP_DBG_OFF
#define	TCP_OUTPUT_DEBUG	LWIP_DBG_OFF
#define	TCP_QLEN_DEBUG		LWIP_DBG_OFF
#define	TCP_RST_DEBUG		LWIP_DBG_OFF
#define	TCP_RTO_DEBUG		LWIP_DBG_OFF
#define	TCP_WND_DEBUG		LWIP_DBG_OFF

#include <ix/stddef.h>
#include <ix/byteorder.h>

#define LWIP_IX

#define LWIP_PLATFORM_BYTESWAP	1
#define LWIP_PLATFORM_HTONS(x) hton16(x)
#define LWIP_PLATFORM_NTOHS(x) ntoh16(x)
#define LWIP_PLATFORM_HTONL(x) hton32(x)
#define LWIP_PLATFORM_NTOHL(x) ntoh32(x)

#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 7
#define TCP_SND_BUF 65536

/*
 * FIXME: TCP_MSS of 8960 causes traffic to get dropped in AWS
 *        perhaps jumbo frames is not supported 
 *        so conservatively set TCP_MSS to 1460 for now
 */
//#define TCP_MSS 8960 /* Originally 1460, but now support jumbo frames */
//#define TCP_MSS 1460
#define TCP_MSS 6000 //TODO: increase this to 9000 after debug AWS issue

//#define TCP_WND  (1024 * TCP_MSS) //Not sure what correct TCP_WND setting should be
//#define TCP_WND  (2048 * 1460) //Not sure what correct TCP_WND setting should be
#define TCP_WND 1<<15

#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_TCP              0
#define TCP_ACK_DELAY (1 * ONE_MS)
#define RTO_UNITS (500 * ONE_MS)

/* EdB 2014-11-07 */
#define LWIP_NOASSERT
#define LWIP_EVENT_API 1
#define LWIP_NETIF_HWADDRHINT 1


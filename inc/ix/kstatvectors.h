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
 * actually not a pragma once file
 */


DEF_KSTATS(none);
DEF_KSTATS(idle);
DEF_KSTATS(user);
DEF_KSTATS(timer);
DEF_KSTATS(timer_collapse);
DEF_KSTATS(print_kstats);
DEF_KSTATS(percpu_bookkeeping);
DEF_KSTATS(tx_reclaim);
DEF_KSTATS(tx_send);
DEF_KSTATS(rx_poll);
DEF_KSTATS(rx_recv);
DEF_KSTATS(bsys);
DEF_KSTATS(timer_tcp_fasttmr);
DEF_KSTATS(timer_tcp_slowtmr);
DEF_KSTATS(eth_input);
DEF_KSTATS(tcp_input_fast_path);
DEF_KSTATS(tcp_input_listen);
DEF_KSTATS(tcp_output_syn);
DEF_KSTATS(tcp_unified_handler);
DEF_KSTATS(timer_tcp_send_delayed_ack);
DEF_KSTATS(timer_handler);
DEF_KSTATS(timer_tcp_retransmit);
DEF_KSTATS(timer_tcp_persist);
DEF_KSTATS(bsys_dispatch_one);
DEF_KSTATS(bsys_tcp_accept);
DEF_KSTATS(bsys_tcp_close);
DEF_KSTATS(bsys_tcp_connect);
DEF_KSTATS(bsys_tcp_recv_done);
DEF_KSTATS(bsys_tcp_reject);
DEF_KSTATS(bsys_tcp_send);
DEF_KSTATS(bsys_tcp_sendv);
DEF_KSTATS(bsys_udp_recv_done);
DEF_KSTATS(bsys_udp_send);
DEF_KSTATS(bsys_udp_sendv);

DEF_KSTATS(posix_syscall);

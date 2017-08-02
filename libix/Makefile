# Copyright 2013-16 Board of Trustees of Stanford University
# Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# A Makefile for libIX.

DPDK	= ../deps/dpdk
DPDK_INC = -I$(DPDK)/build/include -I$(DPDK)/lib/librte_eal/common -I$(DPDK)/drivers/net/ixgbe
DPDK_LIBS=
#DPDK_LIBS+=-Wl,-whole-archive $(DPDK)/build/lib/librte_pmd_ixgbe.a -Wl,-no-whole-archive
DPDK_LIBS+=$(DPDK)/build/lib/librte_mbuf.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_ethdev.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_mempool.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_ring.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_eal.a

DPDK_MACHINE_FLAGS = -march=native -DRTE_MACHINE_CPUFLAG_SSE -DRTE_MACHINE_CPUFLAG_SSE2 -DRTE_MACHINE_CPUFLAG_SSE3 -DRTE_MACHINE_CPUFLAG_SSSE3 -DRTE_MACHINE_CPUFLAG_SSE4_1 -DRTE_MACHINE_CPUFLAG_SSE4_2 -DRTE_MACHINE_CPUFLAG_AES -DRTE_MACHINE_CPUFLAG_PCLMULQDQ -DRTE_MACHINE_CPUFLAG_AVX -DRTE_MACHINE_CPUFLAG_RDRAND -DRTE_MACHINE_CPUFLAG_FSGSBASE -DRTE_MACHINE_CPUFLAG_F16C -DRTE_MACHINE_CPUFLAG_AVX2

INC	= -I. -I../inc $(DPDK_INC)
CC 	= gcc
CFLAGS	= -g -Wall -O3 $(INC) $(DPDK_MACHINE_FLAGS)
AR	= ar

SRCS	= main.c ixev.c ixev_timer.c
OBJS	= $(subst .c,.o,$(SRCS))

all: libix.a

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(foreach SRC,$(SRCS),$(CC) $(CFLAGS) -MM -MT $(SRC:.c=.o) $(SRC) >> .depend;)

-include .depend

libix.a: $(OBJS)
	$(AR) cru $(@) $(OBJS) #$(DPDK_LIBS)

clean:
	rm -f $(OBJS) libix.a .depend

dist-clean: clean
	rm *~


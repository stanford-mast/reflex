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

# A Makefile for IX.
#
# path for this Makefile to work properly.

DPDK	= ../deps/dpdk
SPDK    = ../deps/spdk
DPDK_INC = -I$(DPDK)/build/include -I$(DPDK)/lib/librte_eal/common 
SPDK_INC = -idirafter$(SPDK)/include
INC	= -I../inc -I../inc/lwip -I../inc/lwip/ipv4 -I../inc/lwip/ipv6 $(DPDK_INC) $(SPDK_INC) -I../libix
CC	= gcc

DPDK_MACHINE_FLAGS = -march=native -DRTE_MACHINE_CPUFLAG_SSE -DRTE_MACHINE_CPUFLAG_SSE2 -DRTE_MACHINE_CPUFLAG_SSE3 -DRTE_MACHINE_CPUFLAG_SSSE3 -DRTE_MACHINE_CPUFLAG_SSE4_1 -DRTE_MACHINE_CPUFLAG_SSE4_2 -DRTE_MACHINE_CPUFLAG_AES -DRTE_MACHINE_CPUFLAG_PCLMULQDQ -DRTE_MACHINE_CPUFLAG_AVX -DRTE_MACHINE_CPUFLAG_RDRAND -DRTE_MACHINE_CPUFLAG_FSGSBASE -DRTE_MACHINE_CPUFLAG_F16C -DRTE_MACHINE_CPUFLAG_AVX2
CFLAGS	= -g -w -fno-dwarf2-cfi-asm -fno-asynchronous-unwind-tables -O3 -mno-red-zone $(INC) -D__KERNEL__ $(EXTRA_CFLAGS) -DSPDK_STRING_H $(DPDK_MACHINE_FLAGS)
PCIDMA	= ../deps/pcidma
INC	+= -I$(PCIDMA)
LD	= gcc
LDFLAGS	= -T ix.ld
LDLIBS	= -lrt -lpthread -lm -lnuma -ldl -lconfig -lpciaccess

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG
endif

ifneq ($(ENABLE_KSTATS),)
CFLAGS += -DENABLE_KSTATS
endif

SRCS =
DIRS = core drivers lwip net

define register_dir
SRCS += $(patsubst %, $(1)/%, $(2))
endef

include $(patsubst %, %/dir.mk, $(DIRS))

%.generated.S: %.c
	$(CC) $(CFLAGS) -o $@ -S $<

all: ix

OBJS=$(subst .c,.o,$(SRCS))

depend: .depend

.depend: $(SRCS)
	bash -c "cat $(foreach SRC,$(SRCS),<($(CC) $(CFLAGS) -MM -MT $(SRC:.c=.o) $(SRC))) > ./.depend"

ifneq ($(MAKECMDGOALS),clean)
-include .depend
endif

DPDK_LIBS=
DPDK_LIBS+=-Wl,-whole-archive $(DPDK)/build/lib/librte_pmd_ixgbe.a $(DPDK)/build/lib/librte_pmd_ena.a  -Wl,-no-whole-archive
DPDK_LIBS+=$(DPDK)/build/lib/librte_mbuf.a
DPDK_LIBS+=$(DPDK)/build/lib/libethdev.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_mempool.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_ring.a
DPDK_LIBS+=$(DPDK)/build/lib/librte_eal.a

SPDK_LIBS=$(SPDK)/lib/nvme/libspdk_nvme.a
SPDK_LIBS+=$(SPDK)/lib/util/libspdk_util.a
SPDK_LIBS+=$(SPDK)/lib/memory/libspdk_memory.a

IX_LIBS+=../libix/libix.a

#ix: $(DEPENDENCY) $(OBJS) $(DUNE_LIB) ix.ld
#	$(LD) $(LDFLAGS) -o ix $(OBJS) $(DUNE_LIB) $(SPDK_LIBS) $(DPDK_LIBS) $(LDLIBS) $(IX_LIBS)
ix: $(DEPENDENCY) $(OBJS) ix.ld
	$(LD) $(LDFLAGS) -o ix $(OBJS) $(SPDK_LIBS) $(DPDK_LIBS) $(LDLIBS) $(IX_LIBS)
clean:
	rm -f $(OBJS) ix .depend

dist-clean: clean
	rm *~


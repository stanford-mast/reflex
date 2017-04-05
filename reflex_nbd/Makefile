# Makefile for the fixed_time kernel module
# Adapted from the Dune Kernel Module
# Adapted from the ipw2200 project.

# We have limited control over the build process of merge files - the kernel
# decides what compiler, how to do dependency tracking, etc.

ifndef CONFIG_IX_REFLEX_MOD
EXTERNAL_BUILD=y
CONFIG_IX_REFLEX_MOD=m
endif

#WEIRDness. The name of the module can't be equal to any of the
#constituent .c files, otherwise it only builds that one file.
list-m :=
list-$(CONFIG_IX_REFLEX_MOD) += reflex
obj-$(CONFIG_IX_REFLEX_MOD) += reflex.o
reflex-objs := \
        reflex_nbd.o \

# KSRC should be set to the path to your sources
# modules are installed into KMISC
KVER  := $(shell uname -r)
KSRC := /lib/modules/$(KVER)/build
KMISC := /lib/modules/$(KVER)/kernel/drivers/misc
KMISC_INC := /lib/modules/$(KVER)/include

#SYSCALL_TBL = 0x$(shell $(EXTRACT) sys_call_table)
#DO_FORK = 0x$(shell $(EXTRACT) do_fork)

#Set to empty to print out whole commands for added targets
V := @

ccflags-y :=

ccflags-y += -DDEBUG

ccflags-y += -Wno-unused
ccflags-y += -Wno-declaration-after-statement

ccflags-y += -O3

ccflags-y += -g

#Static verification using smatch
ifdef SMATCH
CHECK := $(SMATCH) --project=kernel --full_path
CC := $(SMATCHCC)
CONFIG_DYNAMIC_DEBUG := n
endif

# KSRC_OUTPUT should be overridden if you are using a 2.6 kernel that
# has it's output sent elsewhere via KBUILD_OUTPUT= or O=
KSRC_OUTPUT := $(KSRC)

PWD=$(shell pwd)

VERFILE := $(KSRC_OUTPUT)/include/linux/version.h
KERNELRELEASE := $(shell \
	if [ -r $(VERFILE) ]; then \
		(cat $(VERFILE); echo UTS_RELEASE) | \
		$(CC) -I$(KSRC_OUTPUT) $(CFLAGS) -E - | \
		tail -n 1 | \
		xargs echo; \
        else \
		uname -r; \
	fi)

MODPATH := $(DESTDIR)/lib/modules/$(KERNELRELEASE)

all: modules

lint: clean
	$(V) $(CPPLINT) \
        --extensions=c,h \
        *.c *.h \
	|& grep -v "Done processing" \
	|& grep -v "Total errors found" \
        || true

clean:
	rm -f *.mod.c *.mod *.o *.ko .*.cmd .*.flags .lst *.lst *.order *.symvers
	rm -f .*.d *.d
	rm -rf $(PWD)/tmp
#	for file in *.{c,h}; do \
#		sed -i -e "s:\ *$$::g" -e "s:\t*$$::g" $$file; \
	done

distclean: clean
	rm -f tags TAGS

TMP=$(PWD)/tmp
MODVERDIR=$(TMP)/.tmp_versions

modules:
	mkdir -p $(MODVERDIR)
#	-cp $(IEEE80211_MODVERDIR)/*.mod $(MODVERDIR)
ifdef ($(KSRC_OUTPUT)/.tmp_versions)
	-cp $(KSRC_OUTPUT)/.tmp_versions/*.mod $(MODVERDIR)
endif
ifeq ($(KSRC),$(KSRC_OUTPUT)) # We're not outputting elsewhere
ifdef ($(KSRC)/.tmp_versions)
	-cp $(KSRC)/.tmp_versions/*.mod $(MODVERDIR)
endif
	$(MAKE) -C $(KSRC) M=$(PWD) MODVERDIR=$(MODVERDIR) modules | tee $(TMP)/make.out
	@if grep -i "WARNING" $(TMP)/make.out > /dev/null; \
	then echo "There were warnings. Aborting."; \
	fi
else # We've got a kernel with seperate output, copy the config, and use O=
	mkdir -p $(TMP)
	cp $(KSRC_OUTPUT)/.config $(TMP)
	$(MAKE) -C $(KSRC) M=$(PWD) MODVERDIR=$(MODVERDIR)) O=$(PWD)/tmp modules
endif

install: modules
	sudo install -d $(KMISC)
	sudo install -m 644 -c $(addsuffix .ko,$(list-m)) $(KMISC)
	sudo cp reflex.h /usr/local/include
	sudo /sbin/depmod -a ${KVER}
	( sudo rmmod $(addsuffix .ko,$(list-m)) >& /dev/null ) || true
	sudo insmod $(addsuffix .ko,$(list-m))
	sleep 1
	sudo chmod ugo+rw /dev/reflex

uninstall:
	rm -rf $(addprefix $(KMISC),$(addsuffix .ko,$(list-m)))
	/sbin/depmod -a ${KVER}


.PHONY: TAGS tags check_inc

RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \) -prune -o
define all-sources
	( find . $(RCS_FIND_IGNORE) -name '*.[chS]' -print )
endef

TAGS:
	$(all-sources) | etags -
tags:
	rm -f $@
	CTAGSF=`ctags --version | grep -i exuberant >/dev/null && echo "-I __initdata,__exitdata,EXPORT_SYMBOL,EXPORT_SYMBOL_NOVERS"`; \
	$(all-sources) | xargs ctags $$CTAGSF -a


PROJECTS=kernel init user

include kbuild.config
export DESTDIR=$(SYSROOT)
export LIBC_DIR?=$(shell realpath ./newlib)

ifeq ($(V),1)
export VERBOSE=1
endif

.PHONY: all clean distclean install install-system
.PHONY: kernel init user copy-headers

all: kernel init

kernel: copy-headers
	$(MAKE) -C kernel

copy-headers:
	$(MAKE) -C kernel install-headers

install: copy-headers
	$(MAKE) -C kernel install

install-system: install init user
	$(MAKE) -C init install

# USERSPACE
init:
	$(MAKE) -C init

user:
	$(MAKE) -C user

# CLEAN
clean:
	@for PROJECT in $(PROJECTS); do \
		echo "CLEAN	$$PROJECT"; \
  		($(MAKE) -s -C $$PROJECT clean) \
	done
	rm -rf sysroot

distclean: clean
	rm -f debug.txt dump.txt kbuild.config* log.txt

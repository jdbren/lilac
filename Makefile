PROJECTS=kernel init user musl

-include kbuild.config
export DESTDIR=$(SYSROOT)

ifeq ($(V),1)
export VERBOSE=1
endif

.PHONY: all clean distclean install install-system
.PHONY: kernel init user copy-headers config

all: kernel

kbuild.config:
	./config.sh

config:
	@if [ ! -f musl/config.mak ]; then \
		cd musl && ./configure --target=x86_64-lilac --prefix=/usr --disable-shared --enable-debug; \
	fi

kernel: copy-headers
	$(MAKE) -C kernel

copy-headers: config
	$(MAKE) -C musl install-headers
	$(MAKE) -C kernel install-headers

install: copy-headers
	$(MAKE) -C kernel install

install-libc: copy-headers
	$(MAKE) -C musl install

install-system: install install-libc init user
	$(MAKE) -C init install

# USERSPACE
init: install-libc
	$(MAKE) -C init

user: install-libc
	$(MAKE) -C user

# CLEAN
clean:
	@for PROJECT in $(PROJECTS); do \
		echo "CLEAN	$$PROJECT"; \
  		($(MAKE) -s -C $$PROJECT clean) \
	done
	rm -rf sysroot

distclean: clean
	@$(MAKE) -C musl distclean
	rm -f debug.txt dump.txt kbuild.config* log.txt

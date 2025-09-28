include kbuild.config
export DESTDIR=$(SYSROOT)
export LIBC_DIR?=$(HOME)/newlib

ifeq ($(V),1)
export VERBOSE=1
endif

.PHONY: all clean kernel init user libc install install-libc install-system copy-headers shell

all: kernel libc init

kernel: copy-headers
	$(MAKE) -C kernel

copy-headers:
	mkdir -p sysroot/usr/include/asm
	$(MAKE) -C kernel install-headers

libc:
	@if [ ! -d build-libc ]; then \
		mkdir -p build-libc; \
		env -i PATH="$$PATH" /bin/sh -c 'cd build-libc; \
		$(LIBC_DIR)/configure \
			--prefix=$(PREFIX) \
			--target=$(TARGET) \
			--disable-multilib \
			--enable-newlib-multithread=no'; \
	fi
	$(MAKE) -s -C build-libc all

install-libc: copy-headers libc
	$(MAKE) -s DESTDIR=$(DESTDIR) -C build-libc install
	cp -r $(DESTDIR)$(PREFIX)/$(TARGET)/* $(SYSROOT)$(PREFIX)/ || true
	rm -rf $(DESTDIR)$(PREFIX)/$(TARGET) || true

init: install-libc
	$(MAKE) -C init

user: install-libc
	$(MAKE) -C user

shell: install-libc
	@if [ -d ../gush ]; then \
		$(MAKE) -C ../gush; \
		mkdir -p ./sysroot/sbin; \
		cp ../gush/gush ./sysroot/sbin/; \
	fi

install:
	$(MAKE) -C kernel install

install-system: install install-libc init user shell
	$(MAKE) -C init install

clean:
	@for PROJECT in $(PROJECTS); do \
  		($(MAKE) -C $$PROJECT clean) \
	done
	@if [ -d ../gush ]; then \
		$(MAKE) -C ../gush clean; \
	fi
	rm -rf build-libc
	rm -rf sysroot

PROJECTS=kernel init user

include kbuild.config
export DESTDIR=$(SYSROOT)
export LIBC_DIR?=$(shell realpath ./newlib)

ifeq ($(V),1)
export VERBOSE=1
endif

.PHONY: all clean distclean install install-libc install-system
.PHONY: kernel init user libc shell copy-headers

all: kernel init libc

kernel: copy-headers
	$(MAKE) -C kernel

copy-headers:
	$(MAKE) -C kernel install-headers

install:
	$(MAKE) -C kernel install

install-system: install install-libc init user shell
	$(MAKE) -C init install

# LIBC
libc: copy-headers
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

install-libc: libc
	$(MAKE) -s DESTDIR=$(DESTDIR) -C build-libc install
	cp -r $(DESTDIR)$(PREFIX)/$(TARGET)/* $(SYSROOT)$(PREFIX)/ || true
	rm -rf $(DESTDIR)$(PREFIX)/$(TARGET) || true

# USERSPACE
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

# CLEAN
clean:
	@for PROJECT in $(PROJECTS); do \
  		($(MAKE) -s -C $$PROJECT clean) \
	done
	@if [ -d ../gush ]; then \
		$(MAKE) -s -C ../gush clean; \
	fi
	rm -rf build-libc
	rm -rf sysroot

distclean: clean
	rm -f debug.txt dump.txt kbuild.config* log.txt

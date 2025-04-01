include kbuild.config
export DESTDIR=$(SYSROOT)
export LIBC_DIR?=$(HOME)/newlib

ifeq ($(V),1)
export VERBOSE=1
endif

.PHONY: all clean init user libc install install-libc install-system

all: lilac.ker

lilac.ker:
	$(MAKE) -C kernel

libc:
	@if [ ! -d build-libc ]; then \
		mkdir -p build-libc; \
		env -i /bin/sh -c 'cd build-libc; \
		$(LIBC_DIR)/configure \
			--prefix=$(PREFIX) \
			--target=$(TARGET) \
			--enable-newlib-multithread=no'; \
	fi
	$(MAKE) -s -C build-libc all

install-libc: libc
	$(MAKE) -s DESTDIR=$(DESTDIR) -C build-libc install
	cp -r $(DESTDIR)$(PREFIX)/$(TARGET)/* $(SYSROOT)$(PREFIX)/ || true
	rm -rf $(DESTDIR)$(PREFIX)/$(TARGET) || true

init: libc
	$(MAKE) -C init

user: libc init
	$(MAKE) -C user

install:
	$(MAKE) -C kernel install

install-system: lilac.ker install-libc init user
	$(MAKE) -C kernel install
	$(MAKE) -C init install

clean:
	@for PROJECT in $(PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) clean) \
	done
	rm -rf build-libc
	rm -rf sysroot

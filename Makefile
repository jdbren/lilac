include kbuild.config
export DESTDIR=$(SYSROOT)

.PHONY: all clean lilac.kernel libk.a install-headers

all: lilac.kernel

lilac.kernel: libk.a
	$(MAKE) -C kernel

libk.a: install-headers
	$(MAKE) -C libc

install: lilac.kernel libk.a
	$(MAKE) -C libc install-libs
	$(MAKE) -C kernel install

install-headers:
	mkdir -p $(SYSROOT)
	$(MAKE) -C libc install-headers

clean:
	for PROJECT in $(PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) clean) \
	done
	rm -rf sysroot

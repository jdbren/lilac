include kbuild.config
export DESTDIR=$(SYSROOT)


.PHONY: all clean lilac.kernel install-headers install-kernel

all: lilac.kernel

libk.a: install-headers
	cd libc &&  $(MAKE) install-libs

lilac.kernel: libk.a
	cd kernel && $(MAKE)

install: install-kernel

install-kernel: lilac.kernel
	cd kernel && $(MAKE) install-kernel

install-headers:
	mkdir -p $(SYSROOT)
	for PROJECT in $(SYSTEM_HEADER_PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) install-headers) \
	done
	mkdir -p $(SYSROOT)/bin
	cp -r $(PWD)/user_code/code $(SYSROOT)/bin

clean:
	for PROJECT in $(PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) clean) \
	done
	rm -rf sysroot
	rm debug.txt

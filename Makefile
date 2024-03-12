include kbuild.config
export DESTDIR=$(SYSROOT)

.PHONY: all clean lilac.kernel install-headers install-kernel

all: lilac.kernel

libk.a: install-headers
	$(MAKE) -C libc install-libs

lilac.kernel: libk.a
	$(MAKE) -C kernel

install: install-kernel

install-kernel: lilac.kernel
	$(MAKE) -C kernel install-kernel

install-headers:
	mkdir -p $(SYSROOT)
	for PROJECT in $(SYSTEM_HEADER_PROJECTS); do \
  		($(MAKE) -C $$PROJECT install-headers) \
	done
	mkdir -p $(SYSROOT)/bin
	cp -r $(PWD)/user_code/code $(SYSROOT)/bin
	cp -r $(PWD)/init/init $(SYSROOT)/bin

clean:
	for PROJECT in $(PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) clean) \
	done

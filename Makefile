include kbuild.config
export DESTDIR=$(SYSROOT)

.PHONY: all clean lilac.ker libk.a install-headers init

all: lilac.ker init

lilac.ker: libk.a
	$(MAKE) -C kernel

libk.a: install-headers
	$(MAKE) -C libc

user: install-headers init
	$(MAKE) -C user

init:
	$(MAKE) -C init

install: lilac.ker libk.a init user
	$(MAKE) -C libc install-libs
	$(MAKE) -C kernel install
	$(MAKE) -C init install

install-headers:
	mkdir -p $(SYSROOT)
	$(MAKE) -C libc install-headers

clean:
	for PROJECT in $(PROJECTS); do \
  		(cd $$PROJECT && $(MAKE) clean) \
	done
	rm -rf sysroot

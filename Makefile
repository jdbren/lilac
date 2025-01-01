include kbuild.config
export DESTDIR=$(SYSROOT)

.PHONY: all clean lilac.ker libk.a install-headers

all: lilac.ker

lilac.ker: libk.a
	$(MAKE) -C kernel

libk.a: install-headers
	$(MAKE) -C libc

user: install-headers
	$(MAKE) -C user

install: lilac.ker libk.a user
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

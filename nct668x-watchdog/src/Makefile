# Makefile to build kernel module

.PHONY: all clean install

obj-m := nuvwdt_668x_lenovo.o
KVERSION := $(shell uname -r)

all: modules

modules:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

install:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules_install

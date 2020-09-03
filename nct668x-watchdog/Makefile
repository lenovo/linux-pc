# The top-level makefile for this project
# 

MAKE = make

.PHONY: all clean install test

all: modules test

clean:
	$(MAKE) -C src clean
	$(MAKE) -C test clean

modules:
	@cd src/ && $(MAKE)

test:
	$(MAKE) -C test

install:
	$(MAKE) -C src install

help:
	@echo
	@echo "Help:"
	@echo "  make         - default, build kernel modules and test program"
	@echo "  make modules - build kernel modules"
	@echo "  make test    - build test program"
	@echo "  make clean   - clean"

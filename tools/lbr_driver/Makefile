.PHONY: all modules install clean

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := simple-pebs.o

ccflags-y += -I$(src)/../../pmu/lbr_driver

# kernel module
all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

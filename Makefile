
MODULE_NAME = asgn2
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs = gpio.o mpq.o dummyport.o
# EXTRA_CFLAGS += -Werror

KDIR    := /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)


all:
        $(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
        $(MAKE) -C $(KDIR) M=$(PWD) clean

help:
        $(MAKE) -C $(KDIR) M=$(PWD) help

install:
        $(MAKE) -C $(KDIR) M=$(PWD) modules_install

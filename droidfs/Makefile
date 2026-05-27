obj-m += droidfs.o

KDIR ?= /home/droidbook/LinuxMIPT-2026/linux-6.18.8
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

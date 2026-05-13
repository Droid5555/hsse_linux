obj-m := tg_fs.o

KDIR  := /home/droidbook/LinuxMIPT-2026/linux-6.18.8/
PWD   := $(shell pwd)
CC    := gcc

all: module server client

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

server: tg_server.c tg_proto.h
	$(CC) -static -o tg_server tg_server.c

client: tg_client.c tg_proto.h
	$(CC) -static -o tg_client tg_client.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f tg_server tg_client

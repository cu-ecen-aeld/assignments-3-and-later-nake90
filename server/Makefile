CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
ASM := $(CROSS_COMPILE)asm
CFLAGS ?= -Wall -g -pthread -O2

.PHONY: clean all default
all: aesdsocket
default: aesdsocket

waesdsocket: waesdsocket.o

clean:
	rm -f waesdsocket *.o

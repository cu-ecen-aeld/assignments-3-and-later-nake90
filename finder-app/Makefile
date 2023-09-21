CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
ASM := $(CROSS_COMPILE)asm
CFLAGS ?= -Wall -g

.PHONY: clean all
all: writer

writer: writer.o

clean:
	rm -f writer *.o

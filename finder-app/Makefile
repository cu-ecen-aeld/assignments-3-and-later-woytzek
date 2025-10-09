CC ?= $(CROSS_COMPILE)gcc

all: build

build:
	$(CC) -c writer.c
	$(CC) writer.o -o writer

clean:
	rm -f writer.o
	rm -f writer


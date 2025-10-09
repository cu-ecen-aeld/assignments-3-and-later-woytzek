COMPILER=$(CROSS_COMPILE)gcc

all: build

build:
	$(COMPILER) -c writer.c
	$(COMPILER) writer.o -o writer

clean:
	rm -f writer.o
	rm -f writer


CC ?= $(CROSS_COMPILE)gcc

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) -g -O0 -Wall -Werror aesdsocket.c -o aesdsocket

clean:
	rm -f aesdsocket.o
	rm -f aesdsocket


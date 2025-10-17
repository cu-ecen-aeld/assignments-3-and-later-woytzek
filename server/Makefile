CC ?= $(CROSS_COMPILE)gcc

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) aesdsocket.o -o aesdsocket

easdsocket.o: aesdsocket.c
	$(CC) -c -Wall -Werror aesdsocket.c

clean:
	rm -f aesdsocket.o
	rm -f aesdsocket


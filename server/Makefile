CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -O0 -Wall -Werror
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket
INCLUDES ?=

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) $^ -o $@

clean:
	rm -f $(TARGET)


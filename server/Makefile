CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -O0 -Wall -Werror
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket
INCLUDES ?=

USE_AESD_CHAR_DEVICE ?= 1

DEFINES += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)
CFLAGS += $(DEFINES)

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) $^ -o $@

clean:
	rm -f $(TARGET)


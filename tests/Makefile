CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter
CPPFLAGS += -D_WIZCHIP_=5500 -D_WIZCHIP_IO_MODE_=0x0203
CPPFLAGS += -I../Ethernet -I../Ethernet/W5500

.PHONY: all clean test

all: test_w5500_atomic_pointer_write

test_w5500_atomic_pointer_write: test_w5500_atomic_pointer_write.c \
		../Ethernet/W5500/w5500.c ../Ethernet/wizchip_conf.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: test_w5500_atomic_pointer_write
	./test_w5500_atomic_pointer_write

clean:
	rm -f test_w5500_atomic_pointer_write

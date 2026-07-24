CBMC ?= cbmc
CFLAGS = -std=c11 -DWIZCHIP_=5500 -D_WIZCHIP_=5500 -D_WIZCHIP_IO_MODE_=0x0203
INCLUDES = -I../../Ethernet -I../../Ethernet/W5500
SRCS = ../../Ethernet/W5500/w5500.c ../../Ethernet/wizchip_conf.c ../../Ethernet/socket.c

.PHONY: cbmc w5500_production_harness
cbmc: w5500_production_harness
	$(CBMC) w5500_production_harness.c $(SRCS) $(INCLUDES) $(CFLAGS) \
		--unwind 5 --bounds-check --pointer-check \
		--memory-leak-check --div-by-zero-check

# Prevent Make's built-in C linker rule from treating the harness as a binary.
w5500_production_harness: w5500_production_harness.c
	@:

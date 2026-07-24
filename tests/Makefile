BUILD_DIR ?= /tmp/opencode/w5500-005/root-gcc
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fno-common
CFLAGS_STRICT = -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -fno-common
CPPFLAGS += -D_WIZCHIP_=5500 -D_WIZCHIP_IO_MODE_=0x0203
CPPFLAGS += -I../Ethernet -I../Ethernet/W5500 -I. -Isupport
LDLIBS ?=

MKDIR_P = mkdir -p

ETH_SRCS = ../Ethernet/W5500/w5500.c ../Ethernet/wizchip_conf.c ../Ethernet/socket.c
SUPPORT_SRC = support/w5500_spi_model.c

TEST_ATOMIC = test_w5500_atomic_pointer_write
TEST_CORRECT = test_w5500_correctness
TEST_SANITIZER = test_public_api_sanitizer
TEST_CONCURRENCY = test_w5500_concurrency
TEST_FAULT = test_w5500_fault_injection
TEST_PHY = test_w5500_phy
TEST_FLAGS = test_w5500_raw_flags
TEST_CONFIG = test_w5500_configuration
TEST_SPI_CONTRACT = test_w5500_spi_contract

CORE_TESTS = $(TEST_ATOMIC) $(TEST_CORRECT) $(TEST_SANITIZER) \
             $(TEST_SPI_CONTRACT)
NEW_TESTS = $(TEST_CONCURRENCY) $(TEST_FAULT) $(TEST_PHY) \
            $(TEST_FLAGS) $(TEST_CONFIG)
ALL_TESTS = $(CORE_TESTS) $(NEW_TESTS)

.PHONY: all test test-us1 sanitize tsan static-analysis cross-compile verify-binaries clean T010
.PHONY: $(ALL_TESTS)

all: $(ALL_TESTS:%=$(BUILD_DIR)/%)

define CORE_RULE
$(BUILD_DIR)/$(1): $(1).c $(ETH_SRCS) $(SUPPORT_SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(1).c $(ETH_SRCS) $(SUPPORT_SRC) $(LDLIBS) -o $$@
endef

define NEW_RULE
$(BUILD_DIR)/$(1): $(1).c $(ETH_SRCS) $(SUPPORT_SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $$(CFLAGS_STRICT) $(1).c $(ETH_SRCS) $(SUPPORT_SRC) $$(LDLIBS) -o $$@
endef

$(foreach t,$(CORE_TESTS),$(eval $(call CORE_RULE,$(t))))
$(foreach t,$(NEW_TESTS),$(eval $(call NEW_RULE,$(t))))

$(foreach t,$(ALL_TESTS),$(eval $(t): $$(BUILD_DIR)/$(t)))

T010: $(TEST_FAULT)

$(BUILD_DIR)/$(TEST_CONCURRENCY): LDLIBS += -lpthread

test-us1: $(BUILD_DIR)/$(TEST_FAULT) $(BUILD_DIR)/$(TEST_CONCURRENCY)
	$(BUILD_DIR)/$(TEST_FAULT)
	$(BUILD_DIR)/$(TEST_CONCURRENCY)

$(BUILD_DIR):
	$(MKDIR_P) $(BUILD_DIR)

test: all
	@fails=0; \
	for t in $(ALL_TESTS); do \
		printf "RUN  %-35s " "$$t"; \
		if $(BUILD_DIR)/$$t > /dev/null 2>&1; then \
			printf "PASS\n"; \
		else \
			printf "FAIL\n"; \
			fails=$$((fails + 1)); \
		fi; \
	done; \
	if [ $$fails -ne 0 ]; then \
		echo "FAILURES: $$fails"; exit 1; \
	else \
		echo "ALL TESTS PASSED"; \
	fi

sanitize: BUILD_DIR := /tmp/opencode/w5500-005/root-sanitize
sanitize:
	@$(MAKE) BUILD_DIR=$(BUILD_DIR) \
		CC=clang \
		CFLAGS='-std=c11 -g -O1 -fsanitize=address,undefined \
		        -fno-sanitize-recover=all \
		        -Wall -Wextra -Werror -Wno-unused-parameter -fno-common' \
		CFLAGS_STRICT='-std=c11 -g -O1 -fsanitize=address,undefined \
		              -fno-sanitize-recover=all \
		              -Wall -Wextra -Werror -Wno-unused-parameter -fno-common \
		              -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith' \
		LDLIBS='-fsanitize=address,undefined' test
	@readelf_path=$$(command -v readelf || true); \
	if [ -n "$$readelf_path" ]; then \
		if ! readelf -lW $(BUILD_DIR)/$(TEST_ATOMIC) | grep -q 'GNU_STACK.*RW '; then exit 1; fi; \
		if readelf -lW $(BUILD_DIR)/$(TEST_ATOMIC) | grep -q 'GNU_STACK.*RWE'; then exit 1; fi; \
	fi
	@nm $(BUILD_DIR)/$(TEST_ATOMIC) | grep -q __asan

tsan: BUILD_DIR := /tmp/opencode/w5500-005/root-tsan
tsan:
	@$(MAKE) BUILD_DIR=$(BUILD_DIR) \
		CC=clang \
		CFLAGS='-std=c11 -g -O1 -fsanitize=thread \
		        -Wall -Wextra -Werror -Wno-unused-parameter -fno-common' \
		CFLAGS_STRICT='-std=c11 -g -O1 -fsanitize=thread \
		              -Wall -Wextra -Werror -Wno-unused-parameter -fno-common \
		              -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith' \
		LDLIBS='-fsanitize=thread -lpthread' test
	@nm $(BUILD_DIR)/test_w5500_concurrency | grep -q __tsan
	@nm $(BUILD_DIR)/test_w5500_concurrency | grep -q __asan && echo 'ERROR: ASan mixed with TSan' && exit 1 || true

static-analysis:
	@echo "=== cppcheck ==="
	-cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction \
	  Ethernet/ 2>&1 || true
	@echo "=== clang-tidy ==="
	-clang-tidy Ethernet/*.c -- -I Ethernet/ -I Ethernet/W5500/ \
	  -D_WIZCHIP_=5500 -D_WIZCHIP_IO_MODE_=0x0203 -std=c11 2>&1 || true

cbmc:
	@echo "cbmc: run via tests/cbmc/Makefile (targets T105)"

cross-compile:
	@echo "cross-compile: run via arm-none-eabi-gcc (targets T107)"

verify-binaries:
	@echo "verify-binaries: run readelf/objdump (targets T108)"

clean:
	rm -rf $(BUILD_DIR)

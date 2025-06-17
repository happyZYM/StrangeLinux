CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
BUILD_DIR = /tmp/mem_test
TARGETS = $(BUILD_DIR)/mem1 $(BUILD_DIR)/mem2 ../build/prog/ramfs_persist_test ../build/prog/mem_official_1 ../build/prog/mem_official_2 ../build/prog/mem_official_3

.PHONY: all clean test test-mem1 test-mem1-sudo test-mem2 test-with-sudo setup

all: setup $(TARGETS)

setup:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p ../build/prog
	@cp ./setup.sh ../build/prog/ramfs_setup.sh
	@chmod +x ../build/prog/ramfs_setup.sh

$(BUILD_DIR)/mem1: mem1.c | setup
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/mem2: mem2.c | setup
	$(CC) $(CFLAGS) -o $@ $< -DTEST_DIR=\"$(BUILD_DIR)/test_files\"

../build/prog/ramfs_persist_test: ramfs_persist_test.c | setup
	$(CC) $(CFLAGS) -o $@ $< -lpthread

../build/prog/mem_official_1: mem_official_1.c | setup
	$(CC) $(CFLAGS) -o $@ $<

../build/prog/mem_official_2: mem_official_2.c | setup
	$(CC) $(CFLAGS) -o $@ $<

../build/prog/mem_official_3: mem_official_3.c | setup
	$(CC) $(CFLAGS) -o $@ $<

# 单独测试mem1（虚拟内存重映射）
test-mem1: $(BUILD_DIR)/mem1
	@echo "=== Running Virtual Memory Remapping Test ==="
	@echo "Note: Run 'make test-mem1-sudo' to see physical addresses"
	@echo "Working directory: $(BUILD_DIR)"
	cd $(BUILD_DIR) && ./mem1

# 使用sudo权限测试mem1（可以看到物理地址）
test-mem1-sudo: $(BUILD_DIR)/mem1
	@echo "=== Running Virtual Memory Remapping Test (with sudo) ==="
	@echo "Working directory: $(BUILD_DIR)"
	cd $(BUILD_DIR) && sudo ./mem1

# 单独测试mem2（文件内存映射）
test-mem2: $(BUILD_DIR)/mem2
	@echo "=== Running File Memory Mapping Test ==="
	@echo "Working directory: $(BUILD_DIR)"
	cd $(BUILD_DIR) && ./mem2

# 运行所有测试（普通权限）
test: test-mem1 test-mem2
	@echo ""
	@echo "=== All Tests Completed ==="
	@echo "To see physical addresses in mem1 test, use: make test-mem1-sudo"

# 运行所有测试（mem1使用sudo，mem2普通权限）
test-with-sudo: test-mem1-sudo test-mem2
	@echo ""
	@echo "=== All Tests Completed (with sudo for mem1) ==="

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Available targets:"
	@echo "  all             - Build all programs in $(BUILD_DIR)"
	@echo "  test            - Run all tests (normal user)"
	@echo "  test-with-sudo  - Run all tests (mem1 with sudo, mem2 normal)"
	@echo "  test-mem1       - Run virtual memory test (normal user)"
	@echo "  test-mem1-sudo  - Run virtual memory test (with sudo for physical addresses)"
	@echo "  test-mem2       - Run file mapping test (normal user)"
	@echo "  clean           - Remove build directory $(BUILD_DIR)"
	@echo "  setup           - Create build directory"
	@echo "  help            - Show this help"
	@echo ""
	@echo "Build directory: $(BUILD_DIR)"
	@echo ""
	@echo "Test separation:"
	@echo "  - mem1: Virtual memory remapping (needs sudo for physical addresses)"
	@echo "  - mem2: File memory mapping (no special privileges needed)" 
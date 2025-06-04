# Makefile for mytcpdump
# 编译自定义tcpdump工具到build目录

# 项目根目录
PROJECT_ROOT := ..
BUILD_DIR := $(PROJECT_ROOT)/build
TARGET := $(BUILD_DIR)/mytcpdump

# 编译器和标志
CC := gcc
CFLAGS := -Wall -Wextra -O2
LDFLAGS := -lpcap

# 源文件
SRCDIR := .
SOURCES := $(SRCDIR)/mytcpdump.c
OBJECTS := $(BUILD_DIR)/mytcpdump.o

# 默认目标
.PHONY: all clean test install

all: $(TARGET)

# 创建build目录
$(BUILD_DIR):
	@echo "Creating build directory..."
	@mkdir -p $(BUILD_DIR)

# 编译目标文件
$(BUILD_DIR)/%.o: $(SRCDIR)/%.c | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# 链接生成可执行文件
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)
	@echo "Build complete: $(TARGET)"

# 清理编译文件
clean:
	@echo "Cleaning build files..."
	@rm -f $(BUILD_DIR)/mytcpdump.o $(TARGET)
	@if [ -d "$(BUILD_DIR)" ] && [ -z "$$(ls -A $(BUILD_DIR))" ]; then \
		rmdir $(BUILD_DIR); \
		echo "Removed empty build directory"; \
	fi


# 测试目标
test: $(TARGET)
	@echo "Running tests..."
	@./run_tests.sh

# 显示帮助信息
help:
	@echo "Available targets:"
	@echo "  all      - Build mytcpdump (default)"
	@echo "  clean    - Remove build files"
	@echo "  test     - Run comparison tests"
	@echo "  help     - Show this help message"

# 调试信息
debug:
	@echo "PROJECT_ROOT: $(PROJECT_ROOT)"
	@echo "BUILD_DIR:    $(BUILD_DIR)"
	@echo "TARGET:       $(TARGET)"
	@echo "SOURCES:      $(SOURCES)"
	@echo "OBJECTS:      $(OBJECTS)" 
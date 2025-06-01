CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread -ldl

BUILD_DIR = ../build/prog
TARGETS = helloworld test1 test2 test3 test4
TARGETS_FULL = $(addprefix $(BUILD_DIR)/,$(TARGETS))

.PHONY: all clean

all: $(TARGETS_FULL)

$(BUILD_DIR)/%: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS_FULL)

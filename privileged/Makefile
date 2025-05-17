CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -static

BUILD_DIR = ../build/prog
TARGET = $(BUILD_DIR)/helloworld
SRC = helloworld.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

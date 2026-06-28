CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -pthread -g
LDFLAGS := -pthread

SRC_DIR := src
BUILD_DIR := build
BIN := traffic-simulator

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: all
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)

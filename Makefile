CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
DEBUG_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -O0 -DDEBUG_TRACE -DDEBUG_STRESS_GC
LDFLAGS = -lm -lpthread
DEBUG_LDFLAGS = -lm -lpthread

SRC_DIR = src
BUILD_DIR = build
TARGET = glipt

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEBUG_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/debug_%.o, $(SRCS))

.PHONY: all debug clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: LDFLAGS = $(DEBUG_LDFLAGS)
debug: $(TARGET)_debug

$(TARGET)_debug: $(DEBUG_OBJS)
	$(CC) $(DEBUG_CFLAGS) $(DEBUG_LDFLAGS) -o $@ $^

$(BUILD_DIR)/debug_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	@echo "Running tests..."
	@./run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TARGET)_debug

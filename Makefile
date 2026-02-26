CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
DEBUG_CFLAGS = -std=c11 -Wall -Wextra -g -O0 -DDEBUG_TRACE -DDEBUG_STRESS_GC
LDFLAGS = -lm -lpthread
DEBUG_LDFLAGS = -lm -lpthread

SRC_DIR = src
MOD_DIR = src/modules
BUILD_DIR = build
TARGET = glipt

SRCS = $(wildcard $(SRC_DIR)/*.c)
MOD_SRCS = $(wildcard $(MOD_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
MOD_OBJS = $(patsubst $(MOD_DIR)/%.c, $(BUILD_DIR)/mod_%.o, $(MOD_SRCS))
DEBUG_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/debug_%.o, $(SRCS))
DEBUG_MOD_OBJS = $(patsubst $(MOD_DIR)/%.c, $(BUILD_DIR)/debug_mod_%.o, $(MOD_SRCS))

.PHONY: all debug clean test

all: $(TARGET)

$(TARGET): $(OBJS) $(MOD_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/mod_%.o: $(MOD_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: LDFLAGS = $(DEBUG_LDFLAGS)
debug: $(TARGET)_debug

$(TARGET)_debug: $(DEBUG_OBJS) $(DEBUG_MOD_OBJS)
	$(CC) $(DEBUG_CFLAGS) -o $@ $^ $(DEBUG_LDFLAGS)

$(BUILD_DIR)/debug_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/debug_mod_%.o: $(MOD_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(TARGET)
	@echo "Running tests..."
	@./run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TARGET)_debug

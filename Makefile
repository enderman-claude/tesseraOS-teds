# teds build and host tests.
CC     ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Iinclude

BUILD := build
LIB_SRC := src/server/server.c src/client/client.c src/transport/loopback.c src/tessera/tessera_stub.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/libteds.a

$(BUILD)/%.o: src/%.c $(wildcard include/teds/*.h)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libteds.a: $(LIB_OBJ)
	ar rcs $@ $^

$(BUILD)/test_teds: tests/test_teds.c $(BUILD)/libteds.a
	$(CC) $(CFLAGS) $< $(BUILD)/libteds.a -o $@

test: $(BUILD)/test_teds
	./$(BUILD)/test_teds

clean:
	rm -rf $(BUILD)

.PHONY: all test clean

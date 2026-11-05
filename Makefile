CC = cc
CPPFLAGS += -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2

PROGRAMS = scheduler worker client
COMMON_OBJECTS = build/common/cli.o build/common/protocol.o build/common/connection.o
OBJECTS = $(COMMON_OBJECTS) $(foreach program,$(PROGRAMS),build/$(program)/main.o)
TEST_BINARY = build/tests/test_transport
TEST_OBJECT = build/tests/test_transport.o

.PHONY: all clean test
all: $(PROGRAMS)

$(PROGRAMS): %: build/%/main.o $(COMMON_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_OBJECT): tests/test_transport.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BINARY): $(TEST_OBJECT) build/common/protocol.o build/common/connection.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TEST_BINARY)
	./$(TEST_BINARY)

clean:
	rm -rf build $(PROGRAMS)

-include $(OBJECTS:.o=.d) $(TEST_OBJECT:.o=.d)

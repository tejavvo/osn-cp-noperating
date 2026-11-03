CC = cc
CPPFLAGS += -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2

PROGRAMS = scheduler worker client
COMMON_OBJECTS = build/common/cli.o
OBJECTS = $(COMMON_OBJECTS) $(foreach program,$(PROGRAMS),build/$(program)/main.o)

.PHONY: all clean
all: $(PROGRAMS)

$(PROGRAMS): %: build/%/main.o $(COMMON_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf build $(PROGRAMS)

-include $(OBJECTS:.o=.d)

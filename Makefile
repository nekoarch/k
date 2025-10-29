CC      ?= clang
CSTD    ?= -std=c11
CFLAGS  ?= -O3 -march=x86-64-v3 $(CSTD) -Wall -Wextra -Wpedantic \
          -Wno-unused-parameter -Wno-sign-compare
LDFLAGS ?= -lm
TARGET  ?= k

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean distclean run repl check install uninstall format

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

repl: run

check: $(TARGET)
	@bash -o pipefail -c './$(TARGET) z.k | diff -u z.out -'

format:
	@command -v clang-format >/dev/null 2>&1 \
		&& clang-format -i $(SOURCES) $(wildcard *.h) \
		|| echo "clang-format not found; skipping format"

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET) a.out dbg.log

distclean: clean
	rm -f z.out.new

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)


CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wformat-security -std=c99 -O2
TARGET = illus
SOURCES = main.c cli.c commands.c
HEADERS = cli.h commands.h

ifeq ($(shell id -u), 0)
    PREFIX ?= /usr/local
else
    PREFIX ?= $(HOME)/.local
endif

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

check: $(TARGET)
	sh tests/integration.sh ./$(TARGET)

unit: $(TARGET)
	$(CC) $(CFLAGS) -std=c99 -I. tests/unit.c commands.c -o tests/unit
	./tests/unit

install: $(TARGET)
	@test -n "$(PREFIX)" || { echo "install: PREFIX must not be empty" >&2; exit 1; }
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Installed $(TARGET) to $(DESTDIR)$(PREFIX)/bin"

uninstall:
	@test -n "$(PREFIX)" || { echo "uninstall: PREFIX must not be empty" >&2; exit 1; }
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Uninstalled $(TARGET) from $(DESTDIR)$(PREFIX)/bin"

clean:
	rm -f $(TARGET)

.PHONY: all check install uninstall clean

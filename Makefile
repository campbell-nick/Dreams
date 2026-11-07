CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wformat-security -std=c99 -O2
TARGET = illus

ifeq ($(shell id -u), 0)
    PREFIX ?= /usr/local
else
    PREFIX ?= $(HOME)/.local
endif

all: $(TARGET)

$(TARGET): main.c commands.c commands.h
	$(CC) $(CFLAGS) main.c commands.c -o $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Installed $(TARGET) to $(DESTDIR)$(PREFIX)/bin"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "Uninstalled $(TARGET) from $(DESTDIR)$(PREFIX)/bin"

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean

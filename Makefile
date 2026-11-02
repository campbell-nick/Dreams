CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O3
TARGET = illus
PREFIX ?= $(HOME)/.local
SRC = main.c commands.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf build

install: illus
	install -d /usr/local/bin
	install -m 755 illus /usr/local/bin/illus
	@echo "[Dreams] Successfully installed illus to /usr/local/bin/"

uninstall:
	rm -f /usr/local/bin/illus
	@echo "[Dreams] Successfully uninstalled illus from /usr/local/bin/"
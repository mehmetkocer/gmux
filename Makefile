# Makefile for gmux - GTK4 VTE Terminal Multiplexer

PKGS = gtk4 vte-2.91-gtk4 json-glib-1.0
VERSION ?= dev
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)

CC = gcc
CPPFLAGS = $(shell pkg-config --cflags $(PKGS))
CPPFLAGS += -DGMUX_VERSION=\"$(VERSION)\" -DGMUX_GIT_COMMIT=\"$(GIT_COMMIT)\"
CFLAGS = -Wall -Wextra
LDLIBS = $(shell pkg-config --libs $(PKGS)) -lm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)
TARGET = gmux

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
APPDIR = $(PREFIX)/share/applications
ICONDIR = $(PREFIX)/share/icons/hicolor/scalable/apps

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -D -m 644 gmux.desktop $(DESTDIR)$(APPDIR)/gmux.desktop
	install -D -m 644 gmux.svg $(DESTDIR)$(ICONDIR)/gmux.svg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(APPDIR)/gmux.desktop
	rm -f $(DESTDIR)$(ICONDIR)/gmux.svg

run: $(TARGET)
	./$(TARGET)

-include $(DEP)

.PHONY: all clean install uninstall run

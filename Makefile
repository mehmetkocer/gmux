# Makefile for gmux - GTK4 VTE Terminal Multiplexer

CC = gcc
CFLAGS = `pkg-config --cflags gtk4 vte-2.91-gtk4 json-glib-1.0` -Wall -Wextra
LDFLAGS = `pkg-config --libs gtk4 vte-2.91-gtk4 json-glib-1.0` -lm

SRC = src/main.c
TARGET = gmux

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
	install -D -m 644 gmux.desktop $(DESTDIR)/usr/local/share/applications/gmux.desktop
	install -D -m 644 gmux.svg $(DESTDIR)/usr/local/share/icons/hicolor/scalable/apps/gmux.svg

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/local/share/applications/gmux.desktop
	rm -f $(DESTDIR)/usr/local/share/icons/hicolor/scalable/apps/gmux.svg

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean install uninstall run

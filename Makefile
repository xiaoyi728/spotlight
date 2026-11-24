CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS   = -lX11 -lXrender -lXext -lm
PREFIX  ?= $(HOME)/.local

spotlight: spotlight.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: spotlight
	install -Dm755 spotlight $(DESTDIR)$(PREFIX)/bin/spotlight
	install -Dm644 contrib/spotlight.desktop $(DESTDIR)$(PREFIX)/share/applications/spotlight.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/spotlight
	rm -f $(DESTDIR)$(PREFIX)/share/applications/spotlight.desktop

clean:
	rm -f spotlight

.PHONY: install uninstall clean

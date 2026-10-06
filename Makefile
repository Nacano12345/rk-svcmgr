# rk-svcmgr
#
# Native:        make
# Static:        make STATIC=1
# Cross (musl):  make CROSS=arm-linux- STATIC=1
# Install:       make install PREFIX=/usr/local

CROSS    ?=
CC       := $(CROSS)gcc
CFLAGS   ?= -O2 -Wall -Wextra
LDFLAGS  += $(if $(STATIC),-static,)
PREFIX   ?= /usr/local
BIN      := svcmgr

all: $(BIN)

$(BIN): svcmgr.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

.PHONY: all clean install

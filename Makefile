# dwm - dynamic window manager
# See LICENSE file for copyright and license details.

include config.mk

INSTALL ?= install
DESTDIR ?=

SRC = drw.c dwm.c icontheme.c util.c
OBJ = ${SRC:.c=.o}

all: dwm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.mk
dwm.o: config.h drw.h icontheme.h util.h
icontheme.o: icontheme.h util.h
drw.o: drw.h util.h
util.o: util.h

dwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f dwm ${OBJ} dwm-${VERSION}.tar.gz

dist: clean
	mkdir -p dwm-${VERSION}
	cp -R LICENSE Makefile README.md config.h config.mk\
		dwm.1 drw.h icontheme.h util.h ${SRC} transient.c patches integration \
		dwm-${VERSION}
	tar -cf dwm-${VERSION}.tar dwm-${VERSION}
	gzip dwm-${VERSION}.tar
	rm -rf dwm-${VERSION}

install: install-wm install-session

# Atomic replace: the running dwm keeps its old inode, so `make install`
# never fails with "Text file busy" and the current session is undisturbed.
install-wm: all
	${INSTALL} -d "${DESTDIR}${BINDIR}" "${DESTDIR}${MANPREFIX}/man1"
	tmp="${DESTDIR}${BINDIR}/dwm.$$$$"; \
	${INSTALL} -m 755 dwm "$${tmp}" && \
	mv -f "$${tmp}" "${DESTDIR}${BINDIR}/dwm"
	tmp="${DESTDIR}${MANPREFIX}/man1/dwm.1.$$$$"; \
	sed "s/VERSION/${VERSION}/g" < dwm.1 > "$${tmp}" && \
	chmod 644 "$${tmp}" && \
	mv -f "$${tmp}" "${DESTDIR}${MANPREFIX}/man1/dwm.1"

# Session files are per-user; skip them for root/system and DESTDIR installs.
install-session:
	@if [ -z "${DESTDIR}" ] && [ "$$(id -u)" -ne 0 ]; then \
		./integration/install.sh; \
	else \
		echo "Skipping per-user session files (run ./integration/install.sh as your user)."; \
	fi

install-local: install-wm

uninstall:
	rm -f "${DESTDIR}${BINDIR}/dwm" \
		"${DESTDIR}${MANPREFIX}/man1/dwm.1"

.PHONY: all clean dist install install-wm install-session install-local uninstall

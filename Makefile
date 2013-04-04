VERSION = $(shell git describe --tags)

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-DLIGHTD_VERSION=\"${VERSION}\" \
	${CFLAGS}

LDFLAGS := -ludev

all: lightd bset

bset: bset.o backlight.o
lightd: lightd.o backlight.o

install: lightd
	install -Dm755  lightd ${DESTDIR}/usr/bin/lightd
	install -Dm5755 bset ${DESTDIR}/usr/bin/bset
	install -Dm644  lightd.service ${DESTDIR}/usr/lib/systemd/system/lightd.service
	install -Dm644  50-synaptics-no-grab.conf ${DESTDIR}/etc/X11/xorg.conf.d/50-synaptics-no-grab.conf

clean:
	${RM} bset lightd *.o

.PHONY: clean install

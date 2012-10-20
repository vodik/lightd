VERSION = $(shell git describe --tags)

CFLAGS := -std=gnu99 \
	-Wall -Wextra -pedantic \
	-DDIMMER_VERSION=\"${VERSION}\" \
	${CFLAGS}

all: dimmer bset

bset: bset.o backlight.o
dimmer: dimmer.o backlight.o

install: dimmer
	install -Dm755  dimmer ${DESTDIR}/usr/bin/dimmer
	install -Dm5755 bset ${DESTDIR}/usr/bin/bset
	install -Dm644  dimmer.service ${DESTDIR}/usr/lib/systemd/system/dimmer.service
	install -Dm644  dimmer.conf ${DESTDIR}/etc/conf.d/dimmer.conf

clean:
	${RM} bset dimmer *.o

.PHONY: clean install

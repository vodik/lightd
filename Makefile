VERSION = $(shell git describe --tags)

CFLAGS := -std=gnu99 \
	-Wall -Wextra -pedantic \
	-DDIMMER_VERSION=\"${VERSION}\" \
	${CFLAGS}

all: dimmer
dimmer: dimmer.o

install: dimmer
	install -Dm755 dimmer ${DESTDIR}/usr/bin/dimmer
	install -Dm644 dimmer.service ${DESTDIR}/usr/lib/systemd/system/dimmer.service

clean:
	${RM} dimmer *.o

.PHONY: clean install

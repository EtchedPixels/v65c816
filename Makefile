.PHONY: lib65816

CFLAGS = -Wall -pedantic -Ilib65816 -g3

all: lib65816 v65 bootstrap

lib65816:
	cd lib65816 && $(MAKE)

v65: platform.o lib65816/src/lib65816.a
	cc -g3 -o $@ $^ -lSDL2

platform.c: config.h

bootstrap: bootstrap.s bootstrap.ld disk0
	cl65 -t none bootstrap.s -C bootstrap.ld
	dd if=bootstrap of=disk0 conv=notrunc

clean:
	rm -f bootstrap *.o *~ v65

os:
	dd if=/tmp/fuzix.img of=disk0 bs=512 seek=65408 conv=notrunc

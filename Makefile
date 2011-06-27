
CFLAGS=-Wall
LDFLAGS=-lconfuse
all: genimage
DEPS = genimage.h list.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

genimage: genimage.o image-jffs2.o image-ext2.o image-ubi.o image-ubifs.o \
	image-flash.o image-file.o image-tar.o image-hd.o util.o config.o

clean:
	rm -f *.o genimage

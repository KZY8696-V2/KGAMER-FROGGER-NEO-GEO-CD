CC = m68k-linux-gnu-gcc
LD = m68k-linux-gnu-ld
OBJCOPY = m68k-linux-gnu-objcopy

# Neodev SDK ve include dizinleri
INCS = -I. -I/usr/include/neodev
LIBS = -L/usr/lib -lneodev

CFLAGS = -O2 -m68k -Wall -fomit-frame-pointer $(INCS)
LDFLAGS = -T neogeo.ld $(LIBS) -nostdlib

all: frogger.elf

frogger.elf: main.o
	$(CC) $(CFLAGS) -o frogger.elf main.o $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

clean:
	rm -f *.o *.elf *.iso

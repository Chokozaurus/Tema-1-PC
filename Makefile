CFLAGS=-g 

all: send recv 

send: send.o lib.o crc.c
	gcc $(CFLAGS) crc.c crc.h send.o lib.o -o send

recv: recv.o lib.o crc.c
	gcc $(CFLAGS) -g crc.c crc.h recv.o lib.o -o recv

.c.o: 
	gcc $(CFLAGS) -Wall -c $? 

clean:
	-rm -f send.o recv.o send recv 

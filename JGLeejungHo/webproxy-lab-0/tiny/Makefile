CC = gcc
CFLAGS = -O0 -Wall -I . -g
# CFLAGS = -O2 -Wall -I . -g

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: tiny echo echoclient cgi

tiny: tiny.c csapp.o
	$(CC) $(CFLAGS) -o tiny tiny.c csapp.o $(LIB)

echo: echo.o csapp.o
	$(CC) $(CFLAGS) -o echo echo.o csapp.os

echoclient: echoclient.o csapp.o
	$(CC) $(CFLAGS) -o echoclient echoclient.o csapp.o

echoserveri: echoserveri.o csapp.o
	$(CC) $(CFLAGS) -o echoserveri echoserveri.o csapp.o
%.o: %.c
	$(CC) $(CFLAGS) -c $<

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

cgi:
	(cd cgi-bin; make)

clean:
	rm -f *.o tiny echo echoclient *~
	(cd cgi-bin; make clean)

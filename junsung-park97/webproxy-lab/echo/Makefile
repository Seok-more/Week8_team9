CC = gcc
CFLAGS = -O0 -Wall -I . -g
# CFLAGS = -O2 -Wall -I . -g

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread

all: client_echo main_echo

main_echo.o: main_echo.c ../csapp.h
	$(CC) $(CFLAGS) -c main_echo.c -o main_echo.o

client_echo.o: client_echo.c ../csapp.h
	$(CC) $(CFLAGS) -c client_echo.c -o client_echo.o

main_echo: main_echo.o ./csapp.o
	$(CC) $(CFLAGS) main_echo.o ./csapp.o -o main_echo

client_echo: client_echo.o ./csapp.o
	$(CC) $(CFLAGS) client_echo.o ./csapp.o -o client_echo

clean:
	rm -f *~ *.o client_echo main_echo


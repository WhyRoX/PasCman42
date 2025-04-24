
CC=gcc
CFLAGS=-std=c17 -pedantic -Wall -Wvla -Werror  -Wno-unused-variable -Wno-unused-but-set-variable -D_DEFAULT_SOURCE

all: exemple

exemple: exemple.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o exemple exemple.o game.o utils_v3.o

exemple.o: ui/exemple.c
	$(CC) $(CFLAGS) -c ui/exemple.c
	
game.o: ui/game.h ui/game.c
	$(CC) $(CFLAGS) -c ui/game.c $(INCLUDES)

utils_v3.o: utils_v3.h utils_v3.c
	$(CC) $(CFLAGS) -c utils_v3.c $(INCLUDES)

clean:
	rm -rf *.o

mrpropre: clean
	rm -rf exemple
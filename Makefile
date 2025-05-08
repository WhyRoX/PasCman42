CC=gcc

CFLAGS=-std=c17 -pedantic -Wall -Wvla -Werror  -Wno-unused-variable -Wno-unused-but-set-variable -D_DEFAULT_SOURCE -g

all: exemple pas_server pas_client broadcaster client_handler

exemple: exemple.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o exemple exemple.o game.o utils_v3.o

exemple.o: exemple.c
	$(CC) $(CFLAGS) -c exemple.c

pas_server: pas_server.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o pas_server pas_server.o game.o utils_v3.o

pas_server.o: pas_server.c
	$(CC) $(CFLAGS) -c pas_server.c

pas_client: pas_client.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o pas_client pas_client.o game.o utils_v3.o

pas_client.o: pas_client.c
	$(CC) $(CFLAGS) -c pas_client.c

broadcaster: broadcaster.o utils_v3.o
	$(CC) $(CFLAGS) -o broadcaster broadcaster.o utils_v3.o

broadcaster.o: broadcaster.c
	$(CC) $(CFLAGS) -c broadcaster.c

client_handler: client_handler.o game.o utils_v3.o
	$(CC) $(CFLAGS) -o client_handler client_handler.o game.o utils_v3.o

client_handler.o: client_handler.c
	$(CC) $(CFLAGS) -c client_handler.c
game.o: game.h game.c
	$(CC) $(CFLAGS) -c game.c $(INCLUDES)

utils_v3.o: utils_v3.h utils_v3.c
	$(CC) $(CFLAGS) -c utils_v3.c $(INCLUDES)

clean: 
	rm -rf *.o

mrpropre: clean
	rm -rf exemple pas_client pas_server broadcaster client_handler

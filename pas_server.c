#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "game.h"
#include "utils_v3.h"

// I set the BACKLOG to 2 because we only have two players
#define BACKLOG 2

int main(int argc, char *argv[]) {
  if (argv == NULL || argc != 3) {
    fprintf(stderr, "Usage: %s <port> <map>\n", argv[0]);
    return EXIT_FAILURE;
  }
  int port = atoi(argv[1]);
  if (port <= 0) {
    fprintf(stderr, "Invalid port number: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  char *mapPath = argv[2];

  struct GameState state;
  FileDescriptor sout = 1;
  FileDescriptor map = sopen(mapPath, O_RDONLY, 0);
  // sclose(map);

  int sockfd = ssocket();
  sbind(port, sockfd);

  slisten(sockfd, BACKLOG);
  printf("Server listening on port %d\n", port);
  printf("With map %s\n", mapPath);
  printf("Waiting for players...\n");

  FileDescriptor player1 = saccept(sockfd);
  char ready = 0;
  if (sread(player1, &ready, sizeof(char)) <= 0 || ready != 1) {
    fprintf(stderr, "Client not ready\n");
    exit(1);
  } else {
    printf("Player 1 is ready\n");
  }

  printf("Player 1 connected\n");
  load_map(map, player1, &state);
  send_registered(1, player1);

  printf("Waiting for player 2...\n");
  FileDescriptor player2 = saccept(sockfd);
  // load_map(map, player1, &state);
  sclose(map);
  printf("Player 2 connected\n");
  return EXIT_SUCCESS;
}

#include "game.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  struct GameState state;
  if (argv == NULL || argc != 3) {
    fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *host = argv[1];
  int port = atoi(argv[2]);

  int sockfd = ssocket();

  sconnect(host, port, sockfd);
  char ready = 1;
  swrite(sockfd, &ready, sizeof(char));
  printf("Connected to server %s on port %d\n", host, port);

  FileDescriptor sout = 1;

  // redirect all socket to sout
  char buffer[1024];
  ssize_t n;
  while ((n = sread(sockfd, buffer, sizeof(buffer))) > 0) {
    swrite(sout, buffer, n);
  }

  return EXIT_SUCCESS;
}

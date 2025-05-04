#include "game.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

#define PAS_CMAN_IPL_PATH "./target/release/pas-cman-ipl"
int main(int argc, char *argv[]) {
  struct GameState state;
  if (argv == NULL || argc != 3) {
    fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *host = argv[1];
  int port = atoi(argv[2]);

  // Exec GUI in ./target/release/pas-cman-ipl
  int pipefd[2];
  int ret = spipe(pipefd);

  int childId = sfork();

  if (childId != 0) {
    // Exec the GUI and replace this proc by it
    // redirect
    FileDescriptor sysin = 0;
    // Redirect stdin to the read end of the pipe
    sdup2(pipefd[0], sysin);
    ret = sclose(pipefd[1]);
    sexecl(PAS_CMAN_IPL_PATH, PAS_CMAN_IPL_PATH, NULL);
    perror("Failed to exec pas-cman-ipl");
    exit(EXIT_FAILURE);
  }

  // Child is connecting to the socket
  // close the read pipe, because we just need to send to the GUI.
  ret = sclose(pipefd[0]);

  // Client Socket init
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
    swrite(pipefd[1], buffer, n);
  }

  return EXIT_SUCCESS;
}

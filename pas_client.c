#include "game.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

#define PAS_CMAN_IPL_PATH "./target/release/pas-cman-ipl"

int main(int argc, char *argv[]) {
  if (argv == NULL || argc != 3) {
    fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *host = argv[1];
  int port = atoi(argv[2]);

  // Exec GUI in ./target/release/pas-cman-ipl
  int sockfd = ssocket();

  // int pipefd[2];
  // int ret = spipe(pipefd);
  int pipefd[2];
  int ret = spipe(pipefd);
  int childId = sfork();
  if (childId != 0) {
    FileDescriptor sysin = 0;
    FileDescriptor sysout = 1;

    // Exec the GUI and replace this proc by it
    // redirect pipefd[0] to stdout
    ret = sclose(pipefd[0]);
    sdup2(pipefd[1], sysout); // pipe[1] -> stdout

    printf("Redirected pipefd[1] to stdout...\n");

    sconnect(host, port, sockfd);
    char ready = 1;
    swrite(sockfd, &ready, sizeof(char));
    printf("Connected to server %s on port %d\n", host, port);

    // Redirect stdin to the read end of the pipe
    printf("Redirecting sockfd to stdin...\n");
    sdup2(sockfd, sysin);
    // ret = sclose(pipefd[1]);
    sexecl(PAS_CMAN_IPL_PATH, PAS_CMAN_IPL_PATH, NULL);

    perror("Failed to exec pas-cman-ipl");
    exit(EXIT_FAILURE);
  }

  // Parent process

  // Close the write pipe, because we just need to read from the GUI.
  // Read from the GUI
  sclose(pipefd[1]);
  char buffer[1024];
  printf("Reading from GUI...\n");
  while (1) {
    ssize_t bytesRead = sread(pipefd[0], buffer, sizeof(buffer));
    if (bytesRead <= 0) {
      break;
    }
    printf("Received from GUI (%ld bytes):", bytesRead);
    for (ssize_t i = 0; i < bytesRead; ++i) {
      printf(" %02x", (unsigned char)buffer[i]);
    }
    printf("\n");
  }

  sclose(sockfd);
  return EXIT_SUCCESS;
}

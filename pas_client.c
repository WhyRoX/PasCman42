#include "game.h"
#include "pascman.h"
#include "pm_exec_paths.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int sockfd = -1;

void send_register(int fd);
void sigint_handler(int signum) {
  printf("\nSIGINT received...\n");
  // Cleanup resources
  if (sockfd != -1) {
    sclose(sockfd);
  }
}
int main(int argc, char *argv[]) {
  if (argv == NULL || argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <host> <port> [-test]\n", argv[0]);
    return EXIT_FAILURE;
  }
  char *host = argv[1];
  int port = atoi(argv[2]);
  if (port <= 0) {
    fprintf(stderr, "Invalid port number: %s\n", argv[2]);
    return EXIT_FAILURE;
  }
  // Check if we're in test mode
  int test_mode = 0;
  if (argc == 4 && strcmp(argv[3], "-test") == 0) {
    test_mode = 1;
    printf("Running in test mode, reading commands from stdin\n");
  }
  // Set up signal handling to ensure cleanup on termination
  signal(SIGINT, sigint_handler);
  // Create a socket
  sockfd = ssocket();
  // Create a pipe for communication with the GUI
  int pipefd[2];
  int ret = spipe(pipefd);
  int childId = sfork();
  if (childId != 0) {
    FileDescriptor sysin = 0;
    FileDescriptor sysout = 1;
    // Exec the GUI and replace this proc by it
    // redirect pipefd[0] to stdout
    ret = sclose(pipefd[0]);
    if (!test_mode) {
      sdup2(pipefd[1], sysout);
    }
    printf("Redirected pipefd[1] to stdout...\n");
    sconnect(host, port, sockfd);
    send_register(sockfd);
    printf("Connected to server %s on port %d\n", host, port);
    // Redirect stdin to the read end of the pipe
    printf("Redirecting sockfd to stdin...\n");
    sdup2(sockfd, sysin);
    sclose(sockfd);
    // ret = sclose(pipefd[1]);
    sexecl(PAS_CMAN_IPL_PATH, PAS_CMAN_IPL_PATH, NULL);
    perror("Failed to exec pas-cman-ipl");
    exit(EXIT_FAILURE);
  }
  // Close the write pipe, because we just need to read from the GUI.
  // Read from the GUI
  sclose(pipefd[1]);
  int buffer[4];
  int fd;
  if (test_mode) {
    fd = STDIN_FILENO;
  } else {
    fd = pipefd[0];
  }

  // random to 100
  long random_val = random() % 100;
  while (1) {
    int bytesRead = sread(fd, buffer, sizeof(buffer));
    if (bytesRead <= 0) {
      break;
    }
    printf("Read %d bytes from GUI\n", bytesRead);
    int key_press = buffer[0];
    printf("Key press: %d\n", key_press);
    printf("[joueur %ld] ", random_val);
    switch (key_press) {
    case UP:
      printf("UP\n");
      break;
    case DOWN:
      printf("DOWN\n");
      break;
    case LEFT:
      printf("LEFT\n");
      break;
    case RIGHT:
      printf("RIGHT\n");
      break;
    default:
      printf("Unknown key press: %d\n", key_press);
      break;
    };

    int bytesWrite = swrite(sockfd, &key_press, sizeof(int));
    if (bytesWrite <= 0) {
      printf("Failed to write to socket\n");
      break;
    }
  }
  sclose(sockfd);
  sockfd = -1;
  return EXIT_SUCCESS;
}
void send_register(int fd) {
  int msg_type = REGISTRATION;
  swrite(fd, &msg_type, sizeof(int));
}

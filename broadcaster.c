#include "common_fd.h"
#include "game.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  printf("Running broadcaster\n");
  if (argv == NULL || argc != 2) {
    fprintf(stderr, "Usage: %s\n", argv[0]);
    return EXIT_FAILURE;
  }
  int players_max = atoi(argv[1]);

  if (players_max <= 0) {
    fprintf(stderr, "Invalid players max number: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  while (1) {
    char buffer[1024];
    ssize_t bytes_read =
        sread(WRITE_PIPE_TO_BROADCAST_FD, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
      perror("Failed to read from pipe");
      break;
    }
    // Process the data read from the pipe
    printf("Received data: %s\n", buffer);
    for (int i = 0; i < players_max; i++) {
      int player_fd = PLAYERS_RANGE_FD + i;
      ssize_t bytes_written = swrite(player_fd, buffer, bytes_read);
      if (bytes_written <= 0) {
        perror("Failed to write to player");
        break;
      }
    }
  }
  for (int i = 0; i < players_max; i++) {
    int player_fd = PLAYERS_RANGE_FD + i;
    sdup2(WRITE_PIPE_TO_BROADCAST_FD, player_fd);
    sclose(player_fd);
  }

  while (1) {
  }
  return EXIT_SUCCESS;
}

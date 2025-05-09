#include "common_fd.h"
#include "game.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

  // do nothing is SIGINT is received
  signal(SIGINT, SIG_IGN);

  printf("Running broadcaster\n");

  while (1) {
    union Message buffer;
    ssize_t bytes_read =
        sread(WRITE_PIPE_TO_BROADCAST_FD, (void *)&buffer, sizeof(buffer));
    if (bytes_read <= 0) {
      perror("Failed to read from pipe");
      break;
    }
    // Process the data read from the pipe
    for (int i = 0; i < NB_PLAYERS; i++) {
      int player_fd = PLAYERS_RANGE_FD + i;
      ssize_t bytes_written = swrite(player_fd, (void *)&buffer, bytes_read);
      if (bytes_written <= 0) {
        perror("Failed to write to player");
        break;
      }
    }

    if (buffer.msgt == GAME_OVER) {
      break;
    }
  }

  printf("Exiting broadcaster\n");
  // Close the pipe
  sclose(WRITE_PIPE_TO_BROADCAST_FD);
  return EXIT_SUCCESS;
}

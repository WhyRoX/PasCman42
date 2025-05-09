#include "common_fd.h"
#include "game.h"
#include "ipc_keys.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>

#define QUIT_VALUE -1

void sigterm_handler(int signum) {
  printf("\nSIGTERM received on client handler...\n");
  // Cleanup resources
  sclose(PLAYER_SOCKET_FD);
  exit(EXIT_SUCCESS);
}
int main(int argc, char *argv[]) {

  // do nothing is SIGINT is received
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, sigterm_handler);
  if (argv == NULL || argc != 2) {
    fprintf(stderr, "Usage: %s\n", argv[0]);
    return EXIT_FAILURE;
  }

  int player_no = atoi(argv[1]);
  enum Item player_it;
  if (player_no == 1) {
    player_it = PLAYER1;
  } else if (player_no == 2) {
    player_it = PLAYER2;
  } else {
    fprintf(stderr, "Invalid player number: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  // Get sem id and shm id
  int sem_id = sem_get(SEM_KEY, 1);
  int shm_id = sshmget(SHM_KEY, sizeof(struct GameState), 0);
  struct GameState *state = sshmat(shm_id);
  // read the fd of the socket
  enum Direction key_press;
  while (sread(PLAYER_SOCKET_FD, &key_press, sizeof(int)) > 0) {
    printf("Received command %d from player %d\n", key_press, player_no);
    // lock semaphore
    sem_down0(sem_id);
    if (process_user_command(state, player_it, key_press,
                             WRITE_PIPE_TO_BROADCAST_FD)) {
      // GAME FINISH
      printf("Detection of the end of the game !\n");
      sem_up0(sem_id);
      return EXIT_SUCCESS;
    }
    sem_up0(sem_id);
  }
  printf("The client handler is closing now.");
  // close the socket
  sclose(PLAYER_SOCKET_FD);
  return EXIT_SUCCESS;
}

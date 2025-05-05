#include "common_fd.h"
#include "game.h"
#include "ipc_keys.h"
#include "pascman.h"
#include "utils_v3.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char *argv[]) {

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

  printf("Yes I'm a client handler of the player %d!\n", player_no);

  // read the fd of the socket
  int key_press;
  while (sread(PLAYER_SOCKET_FD, &key_press, sizeof(int)) > 0) {
    // read the key press
    printf("Key pressed by player %d: %d\n", player_no, key_press);

    // lock semaphore
    sem_down0(sem_id);
    // send the key press to the server
    if (process_user_command(state, player_it, key_press,
                             WRITE_PIPE_TO_BROADCAST_FD)) {
      // GAME FINISH
    }
    sem_up0(sem_id);
  }
  return EXIT_SUCCESS;
}

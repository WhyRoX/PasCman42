#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <unistd.h>

#include "common_fd.h"
#include "game.h"
#include "ipc_keys.h"
#include "pascman.h"
#include "pm_exec_paths.h"
#include "utils_v3.h"

// TODO: Change this to a more appropriate rwx
#define PERM 0666
#define TIMEOUT_SECONDS 5

pid_t first_player_handler_pid = -1;

void timeout_handler(int sig) {
  printf("Temps écoulé ! Pas assez de joueurs.\n");
  if (first_player_handler_pid != -1) {
    skill(first_player_handler_pid, SIGTERM);
  }
}

int main(int argc, char *argv[]) {
  setpgrp();

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
  if (mapPath == NULL) {
    fprintf(stderr, "Invalid map path: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

  /**
   * Create/init shm and sem
   * TODO: SEM to 0 like in the diagram
   * */
  int sem_id = sem_create(SEM_KEY, 1, PERM, 1);
  if (sem_id < 0) {
    perror("Failed to create semaphore");
    exit(EXIT_FAILURE);
  }

  /**
   * Exec broadcaster
   *
   */

  int pipefd[2];
  int ret = spipe(pipefd);
  if (ret < 0) {
    perror("Failed to create pipe");
    exit(EXIT_FAILURE);
  }
  /**
   * First two int are the positions of the first player
   * The next two int are the positions of the second player
   */
  int shm_id = sshmget(SHM_KEY, sizeof(struct GameState), IPC_CREAT | PERM);
  if (shm_id < 0) {
    perror("Failed to get shared memory");
    exit(EXIT_FAILURE);
  }

  struct GameState *state = sshmat(shm_id);

  FileDescriptor sout = 1;
  FileDescriptor map = sopen(mapPath, O_RDONLY, 0);
  int sockfd = ssocket();
  sbind(port, sockfd);

  // The BACKLOG is the maximum number of pending connections
  slisten(sockfd, NB_PLAYERS);
  printf("Server listening on port %d\n", port);
  printf("With map %s\n", mapPath);
  printf("Waiting for players...\n");
  FileDescriptor players_fd[NB_PLAYERS];
  for (int i = 0; i < NB_PLAYERS; i++) {
    printf("Waiting for player %d...\n", i + 1);
    FileDescriptor player = saccept(sockfd);
    int msg_type;
    if (sread(player, &msg_type, sizeof(int)) <= 0 &&
        msg_type != REGISTRATION) {
      fprintf(stderr, "Failed to register the player %d\n", i + 1);
      i -= 1;
      sclose(player);
      continue;
    }

    if (i==0) {
      // Set the timeout handler
      signal(SIGALRM, timeout_handler);
      // Set the alarm to 5 seconds
      alarm(TIMEOUT_SECONDS);
    }
    if (i==1) {
      // Cancel the alarm
      alarm(0);
    }

    printf("Player %d connected\n", i + 1);
    load_map(map, player, state);
    // We need to set the cursor to the beginning of the file after reading the
    // map Lseek (unistd.h) set the cursor to the offset byte (0 in this case)
    // check 'man lseek'
    int lseek_value = lseek(map, 0, SEEK_SET);
    if (lseek_value < 0) {
      perror("Failed to lseek");
      exit(EXIT_FAILURE);
    }

    players_fd[i] = player;
    // create a client_handler for the player in this loop
    int childId = sfork();
    if (childId == 0) {
      // Im gonna sexec(client_handler) but how can I give the sockfd to it ?
      sdup2(player, PLAYER_SOCKET_FD);
      sclose(player);
      sclose(sockfd);

      // exec the client client_handler and replace this child process by
      char player_no[12];
      // TODO: Sprintf is a stdlib function, can we use it ? We need to ask
      // teachers
      sprintf(player_no, "%d", i + 1);
      int exec = sexecl(CLIENT_HANDLER_PATH, CLIENT_HANDLER_PATH, player_no,
                        (char *)NULL);
      if (exec == -1) {
        perror("Failed to exec client_handler");
        exit(EXIT_FAILURE);
      }
    } else { 
      if (i == 0) {
        first_player_handler_pid = childId;
      }
    }
  }
  // send registration to players
  for (int i = 0; i < NB_PLAYERS; i++) {
    send_registered(i + 1, players_fd[i]);
  }
  // End of the loop, all players are connected
  int childId = sfork();
  if (childId == 0) {
    // Redirect stdout to the broadcaster
    sdup2(pipefd[0], WRITE_PIPE_TO_BROADCAST_FD);
    for (int i = 0; i < NB_PLAYERS; i++) {
      sdup2(players_fd[i], PLAYERS_RANGE_FD + i);
      sclose(players_fd[i]);
    }

    int exec = sexecl(BROADCASTER_PATH, BROADCASTER_PATH, (char *)NULL);
    if (exec == -1) {
      perror("Failed to exec broadcaster");
      exit(EXIT_FAILURE);
    }
  }

  // need to unlock ctrl c handling
  while (1) {
    // Wait for the child process to finish
    int wstatus;
    // the pid -1 because "The pid parameter specifies the set of child
    // processes for which to wait. If pid is -1, the call waits for any child
    // process." in man waitpid
    pid_t childId = swaitpid(-1, &wstatus, 0);
    if (childId == -1) {
      perror("Failed to wait for child process");
      exit(EXIT_FAILURE);
    }
    printf("Child process %d finished with status %d\n", childId, wstatus);
  }
  sclose(map);
  sclose(sockfd);
  return EXIT_SUCCESS;
}

int child_handler(void) { return 0; }

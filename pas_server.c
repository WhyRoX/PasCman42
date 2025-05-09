#include <errno.h>
#include <signal.h>
#include <stddef.h>
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

#define PERM 0666
// This is the code when a function has failed but the reason is minor and
// the game loop restart
#define GAME_LOOP_RESTART 3
// If you want to enable the CTRL-C during the game loop, set this to true
#define FORCE_GAME_STOP false
// Timeout in seconds for the game loop used in a Alarm signal handler
#define TIMEOUT 30

#define DEBUG false

int child_handler(void);
int init_ipc(struct GameState **state, int *sem_id, int *shm_id);
int handle_new_players(FileDescriptor *sockfd, struct GameState *state,
                       FileDescriptor *map, FileDescriptor *players_fd,
                       pid_t *client_handlers_pid);

struct GameState *state = NULL;
FileDescriptor map = -1;
FileDescriptor sockfd = -1;
bool sigint_received = false;
pid_t *client_handlers = NULL;
FileDescriptor *players_fd = NULL;
int player_count = 0;
int client_handler_count = 0;
int shm_id = -1;
int sem_id = -1;

void cleanup() {
  printf("Stopping the game...\n");

  printf("- Freeing the state");
  if (state != NULL) {
    free(state);
  }

  printf("- Closing the map...\n");
  if (map != -1) {
    sclose(map);
  }

  printf("- Deleting the shared memory...\n");
  // shm delete
  if (shm_id != -1) {
    sshmdelete(shm_id);
  }

  printf("- Deleting the semaphore...\n");
  // sem delete
  if (sem_id != -1) {
    sem_delete(sem_id);
  }

  printf("- Closing the socket...\n");
  if (sockfd != -1) {
    sclose(sockfd);
  }

  printf("- Closing the player files descriptors...\n");
  if (players_fd != NULL) {
    for (int i = 0; i < player_count; i++) {
      if (players_fd[i] != -1) {
        printf("Closing player %d fd...\n", i);
        sclose(players_fd[i]);
      }
    }
    free(players_fd);
  }

  printf("- Freeing the client handlers pid list...\n");
  if (client_handlers != NULL) {
    free(client_handlers);
  }

  printf("Ressources has been cleaned up\n");

  exit(EXIT_SUCCESS);
}

void sigint_handler(int signum) {
  printf("\nSIGINT received...)\n");
  if ((state != NULL && state->game_over == true) || FORCE_GAME_STOP) {
    cleanup();
    return;
  }
  printf("The game is still running, please wait...\n");
  printf("It will be stopped at the end of the game loop...\n");
  sigint_received = true;
}

void sigalrm_handler(int signum) {
  printf("\nSIGALRM received...)\n");
  printf("No players connected in %d seconds, stopping the game...\n", TIMEOUT);
  cleanup();
}
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
  if (mapPath == NULL) {
    fprintf(stderr, "Invalid map path: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

  /**
   * Create/init shm and sem
   * */
  struct GameState *state = NULL;
  if (init_ipc(&state, &sem_id, &shm_id) != 0) {
    fprintf(stderr, "Failed to initialize IPC\n");
    return EXIT_FAILURE;
  }

  FileDescriptor sout = 1;
  // Create the pipe used by the broadcaster
  int pipefd[2];
  int ret = spipe(pipefd);
  if (ret < 0) {
    perror("Failed to create pipe");
    return EXIT_FAILURE;
  }

  map = sopen(mapPath, O_RDONLY, 0);

  sockfd = ssocket();
  if (sbind(port, sockfd) != 0) {
    perror("Failed to bind socket");
    return EXIT_FAILURE;
  }

  // The BACKLOG is the maximum number of pending connections
  slisten(sockfd, NB_PLAYERS);

  // Set the signal handler for SIGINT
  signal(SIGINT, sigint_handler);

  // Set the signal handler for SIGALRM
  signal(SIGALRM, sigalrm_handler);

  //** Beginning of the game loop
  printf("Server listening on port %d\n", port);
  printf("With map %s\n", mapPath);
  printf("The players have %d seconds to connect\n", TIMEOUT);
  printf("Waiting for players...\n");

  client_handlers = smalloc(NB_PLAYERS * sizeof(pid_t));
  players_fd = smalloc(NB_PLAYERS * sizeof(FileDescriptor));

  while (1) {
    // This is the beginning of the game, so we wait the players,
    // We set an alarm to 30 seconds to stop the game if no players are
    // connected
    alarm(TIMEOUT);

    int handle_players_value =
        handle_new_players(&sockfd, state, &map, players_fd, client_handlers);
    if (handle_players_value != 0) {
      if (handle_players_value == EXIT_FAILURE) {
        printf("Failed to handle new players\n");
        return EXIT_FAILURE;
      }
    }

    // Disable the alarm
    alarm(0);

    // send registration to players
    for (int i = 0; i < NB_PLAYERS; i++) {
      send_registered(i + 1, players_fd[i]);
    }
    // End of the loop, all players are connected

    int broadcastId = sfork();
    if (broadcastId == 0) {
      // Redirect stdout to the broadcaster
      sdup2(pipefd[0], WRITE_PIPE_TO_BROADCAST_FD);
      sclose(pipefd[0]);
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

    int wstatus;
    // the pid -1 because "The pid parameter specifies the set of child
    // processes for which to wait. If pid is -1, the call waits for any child
    // process." in man waitpidhm_id

    // pid_t waitId = swaitpid(-1, &wstatus, 0);
    pid_t waitId;
    do {
      waitId = swaitpid(-1, &wstatus, 0);
    } while (waitId == -1 && errno == EINTR);
    if (waitId == -1) {
      perror("Failed to wait for child process");
      exit(EXIT_FAILURE);
    }
    if (waitId == broadcastId) {
      if (DEBUG) {
        printf("The broadcaster process %d finished with status %d\n", waitId,
               wstatus);
      }
      broadcastId = -1;
    } else {
      for (int i = 0; i < NB_PLAYERS; i++) {
        if (client_handlers[i] == waitId) {
          client_handlers[i] = -1;
          if (DEBUG) {
            printf("The client handler process %d of player %d finished with "
                   "status %d\n",
                   waitId, i + 1, wstatus);
          }
          break;
        }
      }
    }
    if (DEBUG) {
      printf("One of the child process %d finished with status %d\n", waitId,
             wstatus);
    }
    printf("Restarting the game loop...\n");

    for (int i = 0; i < NB_PLAYERS; i++) {
      sclose(players_fd[i]);
      players_fd[i] = -1;
      if (client_handlers[i] != -1) {
        printf("Killing the player %d client handler %d\n", i + 1,
               client_handlers[i]);

        printf("Waiting for the player %d client handler %d to finish\n", i + 1,
               client_handlers[i]);
        // Wait for the client handler to finish
        int wait_client_handler = swaitpid(client_handlers[i], &wstatus, 0);
        printf("The client handler %d of player %d finished with status %d\n",
               wait_client_handler, i + 1, wstatus);
      }
    }
    if (broadcastId != -1) {
      skill(broadcastId, SIGTERM);
      printf("Waiting for the broadcaster process %d to finish\n", broadcastId);
      // Wait for the broadcaster to finish
      int wait_broadcaster = swaitpid(broadcastId, &wstatus, 0);
      printf("The broadcaster process %d finished with status %d\n",
             wait_broadcaster, wstatus);
      broadcastId = -1;
    }
    //  Reset the game state
    sem_down0(sem_id);
    reset_gamestate(state);
    sem_up0(sem_id);

    // Check if the CTRL-C has been called before
    if (sigint_received) {
      printf("SIGINT received, stopping the game...\n");
      cleanup();
    }
  }
  sclose(map);
  sclose(sockfd);
  perror("The while loop has been breaked\n");
  cleanup();
  // Should never reach here
  return EXIT_FAILURE;
}

int child_handler(void) { return 0; }

int init_ipc(struct GameState **state, int *sem_id, int *shm_id) {
  // Create the shared memory segment
  *sem_id = sem_create(SEM_KEY, 1, PERM, 1);
  if (*sem_id < 0) {
    perror("Failed to create semaphore");
    return EXIT_FAILURE;
  }

  *shm_id = sshmget(SHM_KEY, sizeof(struct GameState), IPC_CREAT | PERM);
  if (*shm_id < 0) {
    perror("Failed to get shared memory");
    return EXIT_FAILURE;
  }
  *state = sshmat(*shm_id);
  return 0;
}

int handle_new_players(FileDescriptor *sockfd, struct GameState *state,
                       FileDescriptor *map, FileDescriptor *players_fd,
                       pid_t *client_handlers_pid) {
  for (int i = 0; i < NB_PLAYERS; i++) {
    printf("Waiting for player %d...\n", i + 1);
    FileDescriptor player = saccept(*sockfd);
    int msg_type;
    if (sread(player, &msg_type, sizeof(int)) <= 0 &&
        msg_type != REGISTRATION) {
      fprintf(stderr, "Failed to register the player %d\n", i + 1);
      i -= 1;
      sclose(player);
      continue;
    }
    players_fd[i] = player;
    player_count++;
    printf("Player %d connected\n", i + 1);
    load_map(*map, player, state);
    // We need to set the cursor to the beginning of the file after reading
    // the map Lseek (unistd.h) set the cursor to the offset byte (0 in this
    // case) check 'man lseek'
    int lseek_value = lseek(*map, 0, SEEK_SET);
    if (lseek_value < 0) {
      perror("Failed to lseek");
      return EXIT_FAILURE;
    }

    // create a client_handler for the player in this loop
    client_handlers_pid[i] = sfork();
    if (client_handlers_pid[i] == 0) {
      sdup2(player, PLAYER_SOCKET_FD);
      sclose(player);
      sclose(*sockfd);

      // exec the client client_handler and replace this child process by
      char player_no[12];
      sprintf(player_no, "%d", i + 1);
      int exec = sexecl(CLIENT_HANDLER_PATH, CLIENT_HANDLER_PATH, player_no,
                        (char *)NULL);
      if (exec == -1) {
        perror("Failed to exec client_handler");
        return EXIT_FAILURE;
      }
    }
    client_handler_count++;
  }
  return 0;
}

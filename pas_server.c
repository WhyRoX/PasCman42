#include <errno.h>
#include <signal.h>
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

int child_handler(void);
int init_ipc(struct GameState **state);
int close_all(int fd, ...);
int handle_new_players(FileDescriptor *sockfd, struct GameState *state,
                       FileDescriptor *map, FileDescriptor *players_fd);

struct GameState *state = NULL;
FileDescriptor map = -1;
FileDescriptor sockfd = -1;
bool sigint_received = false;

void cleanup() {
  printf("Stopping the game...\n");
  if (state != NULL) {
    free(state);
  }
  if (map != -1) {
    sclose(map);
  }

  if (sockfd != -1) {
    sclose(sockfd);
  }
  exit(EXIT_SUCCESS);
}

void sigint_handler(int signum) {
  printf("\nSIGINT received...)\n");
  if ((state != NULL && state->game_over == true) || FORCE_GAME_STOP) {
    printf("Stopping the game...\n");
    cleanup();
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
  if (init_ipc(&state) != 0) {
    fprintf(stderr, "Failed to initialize IPC\n");
    return EXIT_FAILURE;
  }
  // Create the pipe used by the broadcaster
  int pipefd[2];
  int ret = spipe(pipefd);
  if (ret < 0) {
    perror("Failed to create pipe");
    exit(EXIT_FAILURE);
  }

  FileDescriptor sout = 1;
  map = sopen(mapPath, O_RDONLY, 0);
  sockfd = ssocket();
  sbind(port, sockfd);

  // The BACKLOG is the maximum number of pending connections
  slisten(sockfd, NB_PLAYERS);

  // Set the signal handler for SIGINT
  signal(SIGINT, sigint_handler);

  // This is the beginning of the game, so we wait the players,
  // We set an alarm to 30 seconds to stop the game if no players are
  // connected
  signal(SIGALRM, sigalrm_handler);
  alarm(TIMEOUT);

  //** Beginning of the game loop
  printf("Server listening on port %d\n", port);
  printf("With map %s\n", mapPath);
  printf("The players have %d seconds to connect\n", TIMEOUT);
  printf("Waiting for players...\n");

  while (1) {
    FileDescriptor *players_fd = smalloc(NB_PLAYERS * sizeof(FileDescriptor));
    int handle_players_value =
        handle_new_players(&sockfd, state, &map, players_fd);
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
      if (errno == EINTR) {
        // The wait was interrupted by a signal, ignore it and continue
        continue;
      }
      perror("Failed to wait for child process");
      exit(EXIT_FAILURE);
    }
    printf("One of the child process %d finished with status %d\n", waitId,
           wstatus);
    printf("Restarting the game loop...\n");

    // Make sure to close the players_fd
    for (int i = 0; i < NB_PLAYERS; i++) {
      sclose(players_fd[i]);
    }
    // Reset the game state
    reset_gamestate(state);

    // Close the broadcast
    sclose(pipefd[0]);
    sclose(pipefd[1]);

    // close the program broadcaster
    skill(broadcastId, SIGTERM);

    // Check if the CTRL-C has been called before
    if (sigint_received) {
      printf("SIGINT received, stopping the game...\n");
      cleanup();
    }
  }
  sclose(map);
  sclose(sockfd);
  // Should never reach here
  return EXIT_FAILURE;
}

int child_handler(void) { return 0; }

int init_ipc(struct GameState **state) {
  // Create the shared memory segment
  int sem_id = sem_create(SEM_KEY, 1, PERM, 1);
  if (sem_id < 0) {
    perror("Failed to create semaphore");
    return EXIT_FAILURE;
  }

  int shm_id = sshmget(SHM_KEY, sizeof(struct GameState), IPC_CREAT | PERM);
  if (shm_id < 0) {
    perror("Failed to get shared memory");
    return EXIT_FAILURE;
  }
  *state = sshmat(shm_id);
  return 0;
}

int handle_new_players(FileDescriptor *sockfd, struct GameState *state,
                       FileDescriptor *map, FileDescriptor *players_fd) {
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

    players_fd[i] = player;
    // create a client_handler for the player in this loop
    int childId = sfork();
    if (childId == 0) {
      // Im gonna sexec(client_handler) but how can I give the sockfd to it ?
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
  }
  return 0;
}

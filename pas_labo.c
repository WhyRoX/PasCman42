#include "game.h"
#include "pascman.h"
#include "pm_exec_paths.h"
#include "utils_v3.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HOST "localhost"
#define TEST_ARG "-test"
#define DEF_BUFFER_DIR_MAX 256

pid_t server_pid;
pid_t *clients_pids;
int total_clients = 0;
FileDescriptor pipe_p1[2]; // Pipe for player 1
FileDescriptor pipe_p2[2]; // Pipe for player 2

int server_run(char *port, char *map);
int client_run(char *port, FileDescriptor *pipefd);
int read_commands(FileDescriptor *file, enum Direction *directions,
                  int *directions_count);
int send_read_instruction(FileDescriptor *file_fd, FileDescriptor *pipe,
                          long *cursor);
int main(int argc, char *argv[]) {
  if (argv == NULL || argc != 5) {
    printf("Usage: %s <port> <map> <input1> <input2>\n", argv[0]);
    return EXIT_FAILURE;
  }

  char *port = argv[1];
  char *map = argv[2];
  char *input1 = argv[3];
  char *input2 = argv[4];

  int ret_p1 = spipe(pipe_p1);
  int ret_p2 = spipe(pipe_p2);

  FileDescriptor input_fd_p1 = sopen(input1, O_RDONLY, 0);
  if (ret_p1 == -1 || ret_p2 == -1) {
    perror("Failed to create pipes");
    return EXIT_FAILURE;
  }
  FileDescriptor input_fd_p2 = sopen(input2, O_RDONLY, 0);
  if (input_fd_p1 == -1 || input_fd_p2 == -1) {
    perror("Failed to open input files");
    return EXIT_FAILURE;
  }
  if (ret_p1 == -1 || ret_p2 == -1) {
    perror("Failed to create pipes");
    return EXIT_FAILURE;
  }
  clients_pids = smalloc(NB_PLAYERS * sizeof(pid_t));

  if (server_run(port, map) != 0) {
    fprintf(stderr, "Failed to start server\n");
    return EXIT_FAILURE;
  }

  if (client_run(port, pipe_p1) != 0) {
    fprintf(stderr, "Failed to start client 1\n");
    return EXIT_FAILURE;
  }

  if (client_run(port, pipe_p2) != 0) {
    fprintf(stderr, "Failed to start client 2\n");
    return EXIT_FAILURE;
  }

  signal(SIGINT, SIG_IGN);

  // Send sig int to the server (when the game is finish it stop)
  if (server_pid != -1) {
    skill(server_pid, SIGINT);
    printf("Sent SIGINT to server %d\n", server_pid);
  }

  printf("Clients started successfully\n");
  long cursor_p1 = 0;
  long cursor_p2 = 0;

  bool end_player_1 = false;
  bool end_player_2 = false;
  while (1) {
    if (end_player_1 && end_player_2) {
      break;
    }
    if (!end_player_1) {
      printf("Waiting for player 1...\n");
      enum Direction direction_p1 =
          send_read_instruction(&input_fd_p1, pipe_p1, &cursor_p1);

      if (direction_p1 == -1) {
        fprintf(stderr, "Failed to read command\n");
        break;
      }
      if (direction_p1 == -2) {
        end_player_1 = true;
      }
    }
    if (!end_player_2) {
      enum Direction direction_p2 =
          send_read_instruction(&input_fd_p2, pipe_p2, &cursor_p2);
      if (direction_p2 == -1) {
        fprintf(stderr, "Failed to read command\n");
        break;
      }
      if (direction_p2 == -2) {
        end_player_2 = true;
      }
    }
  }
  // Close the pipes
  sclose(pipe_p1[0]);
  sclose(pipe_p1[1]);
  sclose(pipe_p2[0]);
  sclose(pipe_p2[1]);
  // Close the input files
  sclose(input_fd_p1);
  sclose(input_fd_p2);

  // 5 seconds
  usleep(5000000);
  // send SIGINT to all clients
  for (int i = 0; i < total_clients; i++) {
    if (clients_pids[i] != -1) {
      skill(clients_pids[i], SIGINT);
      printf("Sent SIGINT to client %d\n", clients_pids[i]);
    }
  }
  free(clients_pids);

  // wait server to finish
  if (server_pid != -1) {
    int wstatus;
    pid_t waitId;
    do {
      waitId = swaitpid(server_pid, &wstatus, 0);
    } while (waitId == -1 && errno == EINTR);
    if (waitId == -1) {
      perror("Failed to wait for server process");
      return EXIT_FAILURE;
    }
    printf("Server process %d finished with status %d\n", waitId, wstatus);
  }
  return EXIT_SUCCESS;
}

int server_run(char *port, char *map) {
  // Placeholder for server run logic
  printf("Server running on port %s with map %s\n", port, map);

  server_pid = fork();

  if (server_pid == 0) {
    // Child process

    sclose(pipe_p1[0]);
    sclose(pipe_p1[1]);
    sclose(pipe_p2[0]);
    sclose(pipe_p2[1]);

    printf("Child server process running...\n");
    int exec = sexecl(PAS_SERVER_PATH, PAS_SERVER_PATH, port, map, NULL);
    if (exec == -1) {
      perror("Failed to exec server");
      return EXIT_FAILURE;
    }
  }
  usleep(200000);
  printf("Server started with PID %d\n", server_pid);
  return 0;
}

int client_run(char *port, FileDescriptor *pipefd) {
  // Placeholder for client run logic
  printf("Client running on port %s\n", port);

  clients_pids[total_clients] = fork();

  if (clients_pids[total_clients++] == 0) {
    // Child process
    printf("Child client %d process running...\n", total_clients);
    // Close the write end of pipe 1, we don't need it in the client
    sclose(pipefd[1]);

    if (pipe_p1[0] != pipefd[0]) {
      sclose(pipe_p1[0]);
    }

    if (pipe_p2[0] != pipefd[0]) {
      sclose(pipe_p2[0]);
    }
    // Duplicate the read end of pipe 1 to stdin
    sdup2(pipefd[0], STDIN_FILENO);
    sclose(pipefd[0]);
    int exec =
        sexecl(PAS_CLIENT_PATH, PAS_CLIENT_PATH, HOST, port, TEST_ARG, NULL);

    if (exec == -1) {
      perror("Failed to exec client");
      return EXIT_FAILURE;
    }
  }
  // Wait 2/10 of a second
  printf("Waiting for client %d to start...\n", total_clients);
  usleep(200000);
  printf("Client %d started with PID %d\n", total_clients,
         clients_pids[total_clients]);
  return 0;
}

enum Direction read_command(FileDescriptor *file_fd, long *cursor) {
  char c;
  ssize_t read_bytes;

  while (1) {
    lseek(*file_fd, *cursor, SEEK_SET);
    printf("Reading command at cursor %ld\n", *cursor);

    read_bytes = sread(*file_fd, &c, sizeof(char));
    if (read_bytes == 0) {
      printf("End of file reached\n");
      return -2; // Fin de fichier
    } else if (read_bytes != sizeof(char)) {
      perror("Read error");
      return -1; // Erreur de lecture
    }

    printf("Read command: %c\n", c);

    switch (c) {
    case 'v':
      (*cursor)++;
      return DOWN;
    case '>':
      (*cursor)++;
      return RIGHT;
    case '<':
      (*cursor)++;
      return LEFT;
    case '^':
      (*cursor)++;
      return UP;
    case '\n':
    default:
      // Incrémente et continue la boucle pour sauter les caractères non
      // pertinents
      (*cursor)++;
      break;
    }
  }
}
int send_read_instruction(FileDescriptor *file_fd, FileDescriptor *pipe,
                          long *cursor) {
  // Placeholder for sending read instruction
  enum Direction direction = read_command(file_fd, cursor++);

  if (direction == -1) {
    fprintf(stderr, "Failed to read command\n");
    return -1;
  }
  if (direction == -2) {
    return -2;
  }
  // Send the command to the client
  swrite(pipe[1], &direction, sizeof(enum Direction));
  printf("Sending command: %d\n", direction);

  usleep(100000);
  return 0;
}

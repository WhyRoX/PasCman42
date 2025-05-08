#ifndef COMMON_FD
#define COMMON_FD

/**
 * This header file contains the definitions of the file descriptors used
 * The file descriptors has been chosen randomly (with command $RANDOM)
 *
 * **/
#define PLAYER_SOCKET_FD 3
#define WRITE_PIPE_TO_BROADCAST_FD 4
// begin to 6 for player 1, 6+1 for player 2, 6+x for player x+1
#define PLAYERS_RANGE_FD 6

#endif // COMMON_FD

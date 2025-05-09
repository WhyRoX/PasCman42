#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>

#include "../utils_v3.h"
// #include "../ui/pascman.h"

#define MAX_CLIENTS 2

typedef struct Player {
    int id;
    int sockfd;
} Player;

volatile sig_atomic_t end = 0;
void endServerHandler(int sig) {
    end = 1;
}

void terminate(Player *tabPlayer, int nbPlayers)
{
    printf("\nJoueurs inscrits :\n");
    for (int i = 0; i < nbPlayers; i++) {
        printf("Joueur %d : %d\n", tabPlayer[i].id, tabPlayer[i].sockfd);
    }
    exit(0);
}

int main(int argc, char **argv)
{
    while(!end)
    {
         here cursor will work ma boi
    }
}
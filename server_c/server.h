#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "../protocol.h"
#include <stdbool.h>

struct Engine;

typedef struct Connection {
    int socket;
    char left_buffer[100000];
    size_t left_buffer_len;
    size_t accepted_order;
} Connection;

typedef struct Game {
    char status[100];
    uint32_t game_id;
    Connection player1;
    Connection player2;
} Game;

typedef struct Server {
    int listening_socket;
    Game games[100];
    pthread_mutex_t game_engine_mutex;
    pthread_mutex_t game_array_mutex;
    int game_count;
    struct Engine* engine;
} Server;

typedef struct RequestThreadArgs {
    Server *server;
    Connection *client;
} RequestThreadArgs;

typedef struct GameThreadArgs {
    Server *server;
    Game *game;
} GameThreadArgs;

Server *server_init(uint16_t port);
void server_close(Server *server);

void *request_handler(void *arg);
void *game_play(void *arg);

bool accept_ship_placement(Game *game, Server *server);
void send_failure(Game *game);
void end_game_after_failure(Server *server, Game *game);

Message *read_one_message(Connection *connection);
int write_all(int socket, const void *data, size_t length);

#endif
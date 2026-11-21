#include <stdio.h>
#include <stdlib.h>
#include "../protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "server.h"
#include "engine.h"
#include <signal.h>
#include <errno.h>
#include <pthread.h>

// MULTIPLE_GAMES

Server* server_init(uint16_t port){
    Server* ptr = malloc(sizeof(Server));
    if (ptr == NULL){
        exit(5);
    }
    ptr->listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ptr->listening_socket < 0){
        exit(3);
    }
    int enable = 1;
    if (setsockopt(ptr->listening_socket, SOL_SOCKET, SO_REUSEADDR,
        &enable, sizeof(enable)) < 0) {
            perror("setsockopt");
            exit(3);
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(ptr->listening_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0){
        exit(3);
    }

    if (listen(ptr->listening_socket, 10) < 0){
        exit(3);
    };
    // Assign default values
    ptr->game_count = 0;
    pthread_mutex_init(&ptr->game_engine_mutex, NULL);
    pthread_mutex_init(&ptr->game_array_mutex, NULL);

    return ptr;
}

bool accept_ship_placement(Game* game, Server* server){
    Message* message1 = read_one_message(&game->player1);
    if (message1 == NULL) {
        return false;
    }
    if (strcmp(message1->type, "SHIP_PLACEMENT") != 0) {
        free(message1->content);
        free(message1);
        return false;
    }
    char buffer1[1000];
    memcpy(buffer1, message1->content, message1->length);
    buffer1[message1->length] = '\0';
    char* next_ptr1;
    // Extract one ship information that ends with /
    char* one_ship_info1 = strtok_r(buffer1, "/", &next_ptr1);
    struct Ship ships1[4];
    char coordinates1[4][10];
    int i = 0;
    while(one_ship_info1 != NULL){
        int length;
        char direction;

        int parsed = sscanf(one_ship_info1, "%9[^,],%d,%c",
            coordinates1[i], &length, &direction);

        if (parsed == 3){
            ships1[i].coordinate = coordinates1[i];
            ships1[i].length = length;
            if (direction == 'H'){
                ships1[i].direction = Horizontal;
            }
            else{
                ships1[i].direction = Vertical;
            }
        }
        else{
            free(message1->content);
            free(message1);
            return false;
        }
        one_ship_info1 = strtok_r(NULL, "/", &next_ptr1);
        i++;
    }
    pthread_mutex_lock(&server->game_engine_mutex);
    int player_number = engine_place_ships(server->engine, game->game_id, &ships1);
    pthread_mutex_unlock(&server->game_engine_mutex);
    if (player_number == -1){
        free(message1->content);
        free(message1);
        return false;
    }else{
        char sending_message1[1000];
        snprintf(sending_message1, sizeof(sending_message1),
         "TYPE:PLAYER_NUMBER\r\nLENGTH:1\r\n\r\n%d",
         player_number);
        write_all(game->player1.socket, sending_message1, strlen(sending_message1));
        free(message1->content);
        free(message1);
    }

    Message* message2 = read_one_message(&game->player2);
    if (message2 == NULL) {
        return false;
    }
    if (strcmp(message2->type, "SHIP_PLACEMENT") != 0) {
        free(message2->content);
        free(message2);
        return false;
    }
    char buffer2[1000];
    memcpy(buffer2, message2->content, message2->length);
    buffer2[message2->length] = '\0';
    char* next_ptr2;
    char* one_ship_info2 = strtok_r(buffer2, "/", &next_ptr2);
    struct Ship ships2[4];
    char coordinates2[4][10];
    i = 0;
    while(one_ship_info2 != NULL){
        int length;
        char direction;

        int parsed = sscanf(one_ship_info2, "%9[^,],%d,%c",
            coordinates2[i], &length, &direction);

        if (parsed == 3){
            ships2[i].coordinate = coordinates2[i];
            ships2[i].length = length;
            if (direction == 'H'){
                ships2[i].direction = Horizontal;
            }
            else{
                ships2[i].direction = Vertical;
            }
        }
        else{
            free(message2->content);
            free(message2);
            return false;
        }
        one_ship_info2 = strtok_r(NULL, "/", &next_ptr2);
        i++;
    }
    pthread_mutex_lock(&server->game_engine_mutex);
    player_number = engine_place_ships(server->engine, game->game_id, &ships2);
    pthread_mutex_unlock(&server->game_engine_mutex);
    if (player_number == -1){
        free(message2->content);
        free(message2);
        return false;
    }else{
        char sending_message2[1000];
        snprintf(sending_message2, sizeof(sending_message2),
         "TYPE:PLAYER_NUMBER\r\nLENGTH:1\r\n\r\n%d",
         player_number);
        write_all(game->player2.socket, sending_message2, strlen(sending_message2));
        free(message2->content);
        free(message2);
        return true;
    }
}

// Close the sockets to clients and initialize the game data
void server_close(Server* server) {
    for (int i = 0; i < server->game_count; i++){
        close(server->games[i].player1.socket);
        close(server->games[i].player2.socket);
        server->games[i].player1.left_buffer_len = 0;
        server->games[i].player2.left_buffer_len = 0;
        server->games[i].player1.socket = -1;
        server->games[i].player2.socket = -1;
    }
    free(server);
}

// A thread handling request and create a game play thread
void *request_handler(void *arg) {
    RequestThreadArgs *request_args = (RequestThreadArgs *)arg;
    Connection* connection = request_args->client;
    Server* server = request_args->server;
    free(request_args);

    Message *message = read_one_message(connection);
    if (message == NULL){
        char *msg = "TYPE:FAILURE\r\nLENGTH:0\r\n\r\n";
        write_all(connection->socket, msg, strlen(msg));
        free(connection);
        return NULL;
    }
    if (strcmp(message->type, "GAME_IDENTIFIER") != 0){
        free(connection);
        free(message->content);
        free(message);
        return NULL;
    }
    if (message->length != 4) {
        free(connection);
        free(message->content);
        free(message);
        return NULL;
    }
    
    uint32_t net_game_id;
    memcpy(&net_game_id, message->content, 4);
    uint32_t game_id = ntohl(net_game_id);
    pthread_mutex_lock(&server->game_array_mutex);
    // Check if there is any matching game that already exist
    for (int i = 0; i < server->game_count; i++){
        if (server->games[i].game_id == game_id && strcmp(server->games[i].status, "WAITING") == 0){
            Game* game = &server->games[i];
            // Ensure player1/player2 is aligned with the order they got accepted by the server
            if (connection->accepted_order < game->player1.accepted_order){
                game->player2 = game->player1;
                game->player1 = *connection;
                free(connection);
            }
            else{
                game->player2 = *connection;
                free(connection);
            }
            pthread_t thread_id;
            GameThreadArgs *game_thread_args = malloc(sizeof(GameThreadArgs));
            game_thread_args->server = server;
            game_thread_args->game = game;
            strcpy(game->status, "STARTED");
            pthread_mutex_unlock(&server->game_array_mutex);

            pthread_create(&thread_id, NULL, game_play, game_thread_args);
            pthread_detach(thread_id);
            free(message->content);
            free(message);
            return NULL;
        }
        // There is a matching game but it has 2 players already
        else if(server->games[i].game_id == game_id && strcmp(server->games[i].status, "STARTED") == 0){
            const char* sending_message =
            "TYPE:FAILURE\r\nLENGTH:0\r\n\r\n";
            write_all(connection->socket, sending_message, strlen(sending_message));
            pthread_mutex_unlock(&server->game_array_mutex);
            free(message->content);
            free(message);
            free(connection);
            return NULL;
        }
        // Playing game that has finished again
        else if(server->games[i].game_id == game_id && strcmp(server->games[i].status, "FINISHED") == 0){
            Game *game = &server->games[i];
            strcpy(game->status, "WAITING");
            game->player1 = *connection;
            free(connection);
            pthread_mutex_lock(&server->game_engine_mutex);
            if (!engine_init_game(server->engine, game->game_id)){
                const char *msg = "TYPE:FAILURE\r\nLENGTH:0\r\n\r\n";
                write_all(game->player1.socket, msg, strlen(msg));
                strcpy(game->status, "FINISHED");
                pthread_mutex_unlock(&server->game_array_mutex);
                pthread_mutex_unlock(&server->game_engine_mutex);
                free(message->content);
                free(message);
                return NULL;
            }
            pthread_mutex_unlock(&server->game_array_mutex);
            pthread_mutex_unlock(&server->game_engine_mutex);
            free(message->content);
            free(message);
            return NULL;
        }
    }
    // When there exists no matching game, just create a game struct and wait for another client
    Game *game = &server->games[server->game_count];
    strcpy(game->status, "WAITING");
    game->player1 = *connection;
    free(connection);
    game->game_id = game_id;
    server->game_count = server->game_count + 1;
    pthread_mutex_lock(&server->game_engine_mutex);
    if (!engine_init_game(server->engine, game->game_id)){
        const char *msg = "TYPE:FAILURE\r\nLENGTH:0\r\n\r\n";
        write_all(game->player1.socket, msg, strlen(msg));
        strcpy(game->status, "FINISHED");
        pthread_mutex_unlock(&server->game_array_mutex);
        pthread_mutex_unlock(&server->game_engine_mutex);
        free(message->content);
        free(message);
        return NULL;
    }
    pthread_mutex_unlock(&server->game_array_mutex);
    pthread_mutex_unlock(&server->game_engine_mutex);
    free(message->content);
    free(message);
    return NULL;
}

void *game_play(void *arg) {
    GameThreadArgs *game_thread_args = (GameThreadArgs *)arg;
    Game* game = game_thread_args->game;
    Server* server = game_thread_args->server;
    free(game_thread_args);
    struct Engine* engine = server->engine;
    // Player 1 alreadys plays first
    uint8_t turn = 1;

    const char* sending_message =
        "TYPE:CONNECTION_STATUS\r\nLENGTH:5\r\n\r\nREADY";
    write_all(game->player1.socket, sending_message, strlen(sending_message));
    write_all(game->player2.socket, sending_message, strlen(sending_message));
    if (!accept_ship_placement(game, server)){
        end_game_after_failure(server, game);
        return NULL;
    }

    while(1){
        if (turn == 1){
            Message* message1 = read_one_message(&game->player1);
            if (message1 == NULL) {
                end_game_after_failure(server, game);
                return NULL;
            }
            char coordinate[20];
            memcpy(coordinate, message1->content, message1->length);
            coordinate[message1->length] = '\0';
            enum TurnResult result = Invalid;
            if (strcmp(message1->type, "PLAYER_MOVE") == 0) {
                pthread_mutex_lock(&server->game_engine_mutex);
                result = engine_take_turn(engine, game->game_id, turn, coordinate);
                pthread_mutex_unlock(&server->game_engine_mutex);
                if (result == Invalid){
                    send_failure(game);
                    free(message1->content);
                    free(message1);
                    return NULL;
                }
                char content_player[1000];
                snprintf(content_player, sizeof(content_player),"%d",result);
                char header_player[1000];
                snprintf(header_player, sizeof(header_player),
                "TYPE:TURN_RESULT\r\nLENGTH:%zu\r\n\r\n"
                ,strlen(content_player));
                write_all(game->player1.socket, header_player, strlen(header_player));
                write_all(game->player1.socket, content_player, strlen(content_player));
            }
            else if(strcmp(message1->type, "PLAYER_MOVE_EXTENDED") == 0){
                pthread_mutex_lock(&server->game_engine_mutex);
                struct ExtendedTurnResult extended_result = engine_take_turn_extended(engine, game->game_id, turn, coordinate);
                result = engine_extract_turn_result(extended_result);
                if (result == Invalid){
                    send_failure(game);
                    pthread_mutex_unlock(&server->game_engine_mutex);
                    free(message1->content);
                    free(message1);
                    pthread_mutex_lock(&server->game_engine_mutex);
                    engine_free_extended_result(extended_result);
                    pthread_mutex_unlock(&server->game_engine_mutex);
                    return NULL;
                }
                pthread_mutex_unlock(&server->game_engine_mutex);
                char header_player[1000];
                snprintf(header_player, sizeof(header_player),
                "TYPE:TURN_RESULT_EXTENDED\r\nLENGTH:%d\r\n\r\n"
                ,extended_result.length);
                write_all(game->player1.socket, header_player, strlen(header_player));
                write_all(game->player1.socket, extended_result.data, extended_result.length);
                pthread_mutex_lock(&server->game_engine_mutex);
                engine_free_extended_result(extended_result);
                pthread_mutex_unlock(&server->game_engine_mutex);
            }
            else{
                end_game_after_failure(server, game);
                free(message1->content);
                free(message1);
                return NULL;
            }

            char content_opponent[1000];
            snprintf(content_opponent, sizeof(content_opponent),
            "%s,%d",coordinate, result);
            char header_opponent[1000];
            snprintf(header_opponent, sizeof(header_opponent),
            "TYPE:OPPONENT_MOVE\r\nLENGTH:%zu\r\n\r\n",strlen(content_opponent));
            write_all(game->player2.socket, header_opponent, strlen(header_opponent));
            write_all(game->player2.socket, content_opponent, strlen(content_opponent));

            if (result == Win){
                pthread_mutex_lock(&server->game_engine_mutex);
                engine_end_game(engine, game->game_id);
                pthread_mutex_unlock(&server->game_engine_mutex);
                pthread_mutex_lock(&server->game_array_mutex);
                strcpy(game->status, "FINISHED");
                pthread_mutex_unlock(&server->game_array_mutex);
                free(message1->content);
                free(message1);
                return NULL;
            }
            if (result != Invalid) {
                turn = 2;
            }
            free(message1->content);
            free(message1);
        }
        // Player 2's turn
        else{
            Message* message2 = read_one_message(&game->player2);
            if (message2 == NULL) {
                end_game_after_failure(server, game);
                return NULL;
            }
            char coordinate[20];
            memcpy(coordinate, message2->content, message2->length);
            coordinate[message2->length] = '\0';
            enum TurnResult result = Invalid;
            if (strcmp(message2->type, "PLAYER_MOVE") == 0) {
                pthread_mutex_lock(&server->game_engine_mutex);
                result = engine_take_turn(engine, game->game_id, turn, coordinate);
                pthread_mutex_unlock(&server->game_engine_mutex);
                if (result == Invalid){
                    send_failure(game);
                    free(message2->content);
                    free(message2);
                    return NULL;
                }
                char content_player[1000];
                snprintf(content_player, sizeof(content_player),
                "%d",result);
                char header_player[1000];
                snprintf(header_player, sizeof(header_player),
                "TYPE:TURN_RESULT\r\nLENGTH:%zu\r\n\r\n"
                ,strlen(content_player));
                write_all(game->player2.socket, header_player, strlen(header_player));
                write_all(game->player2.socket, content_player, strlen(content_player));
            }
            else if(strcmp(message2->type, "PLAYER_MOVE_EXTENDED") == 0){
                pthread_mutex_lock(&server->game_engine_mutex);
                struct ExtendedTurnResult extended_result = engine_take_turn_extended(engine, game->game_id, turn, coordinate);
                result = engine_extract_turn_result(extended_result);
                pthread_mutex_unlock(&server->game_engine_mutex);
                if (result == Invalid){
                    send_failure(game);
                    free(message2->content);
                    free(message2);
                    pthread_mutex_lock(&server->game_engine_mutex);
                    engine_free_extended_result(extended_result);
                    pthread_mutex_unlock(&server->game_engine_mutex);
                    return NULL;
                }
                char header_player[1000];
                snprintf(header_player, sizeof(header_player),
                "TYPE:TURN_RESULT_EXTENDED\r\nLENGTH:%d\r\n\r\n"
                ,extended_result.length);
                write_all(game->player2.socket, header_player, strlen(header_player));
                write_all(game->player2.socket, extended_result.data, extended_result.length);
                pthread_mutex_lock(&server->game_engine_mutex);
                engine_free_extended_result(extended_result);
                pthread_mutex_unlock(&server->game_engine_mutex);
            }
            else{
                end_game_after_failure(server, game);
                free(message2->content);
                free(message2);
                return NULL;
            }

            char content_opponent[1000];

            snprintf(content_opponent, sizeof(content_opponent),
            "%s,%d", coordinate, result);
            char header_opponent[1000];
            snprintf(header_opponent, sizeof(header_opponent),
            "TYPE:OPPONENT_MOVE\r\nLENGTH:%zu\r\n\r\n", strlen(content_opponent));
            write_all(game->player1.socket, header_opponent, strlen(header_opponent));
            write_all(game->player1.socket, content_opponent, strlen(content_opponent));

            if (result == Win){
                pthread_mutex_lock(&server->game_engine_mutex);
                engine_end_game(engine, game->game_id);
                pthread_mutex_unlock(&server->game_engine_mutex);
                pthread_mutex_lock(&server->game_array_mutex);
                strcpy(game->status, "FINISHED");
                pthread_mutex_unlock(&server->game_array_mutex);
                free(message2->content);
                free(message2);
                return NULL;
            }
            if (result != Invalid) {
                turn = 1;
            }
            free(message2->content);
            free(message2);
        }
    }

}

void send_failure(Game *game){
    char *msg = "TYPE:FAILURE\r\nLENGTH:0\r\n\r\n";
    write_all(game->player1.socket, msg, strlen(msg));
    write_all(game->player2.socket, msg, strlen(msg));
}

void end_game_after_failure(Server *server, Game *game){
    pthread_mutex_lock(&server->game_engine_mutex);
    engine_end_game(server->engine, game->game_id);
    pthread_mutex_unlock(&server->game_engine_mutex);
    send_failure(game);
    pthread_mutex_lock(&server->game_array_mutex);
    strcpy(game->status, "FINISHED");
    pthread_mutex_unlock(&server->game_array_mutex);
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 2) {
        exit(1);
    }
    int port = atoi(argv[1]);
    Server* server = server_init(port);

    struct Engine* engine = engine_init();
    if (engine == NULL){
        exit(2);
    };
    server->engine = engine;
    // Keep track of the count to see which client is accepted
    size_t accepted_count = 0;
    // Keep accepting client requests and create a request_handler thread for concurrency
    while(1){
        int client_socket = accept(server->listening_socket, NULL, NULL);
        if (client_socket < 0){
            close(client_socket);
            continue;
        }
        Connection *temp_client = malloc(sizeof(Connection));
        if (temp_client == NULL){
            close(client_socket);
            exit(5);
        }
        accepted_count += 1;
        temp_client->socket = client_socket;
        temp_client->left_buffer_len = 0;
        temp_client->accepted_order = accepted_count;

        RequestThreadArgs *request_args = malloc(sizeof(RequestThreadArgs));
        request_args->server = server;
        request_args->client = temp_client;
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, request_handler, request_args);
        pthread_detach(thread_id);
    }
    return 0;
}

Message* read_one_message(Connection *connection){
    char temp_buffer[100];
    char app_buffer[100000]; 
    size_t app_len = 0;
    // Extract the buffer data that was overread last time
    memcpy(app_buffer + app_len, connection->left_buffer, connection->left_buffer_len);
    app_len += connection->left_buffer_len;
    app_buffer[app_len] = '\0';

    // Read the header that ends with \r\n\r\n
    while (strstr(app_buffer, "\r\n\r\n") == NULL) {
        ssize_t n = read(connection->socket, temp_buffer, sizeof(temp_buffer));
        if (n <= 0) {
            return NULL;
        }
        memcpy(app_buffer + app_len, temp_buffer, n);
        app_len += n;
        app_buffer[app_len] = '\0'; 
    }

    // Points to the end of the header
    int header_end_length = 4;
    char* header_end = strstr(app_buffer, "\r\n\r\n") + header_end_length;
    int header_size = (header_end - app_buffer);
    // Pointer to the start of the length value
    char* length_start = strstr(app_buffer, "LENGTH:")+7;
    char length[100];
    int i = 0;
    // Read length data until it reaches the content
    while (*length_start != '\r'){
        length[i] = *length_start;
        length_start+=1;
        i+=1;
    }
    length[i] = '\0';
    size_t content_length = atoi(length);
    char* type_start = strstr(app_buffer, "TYPE:")+5;
    char type[100];
    i = 0;
    // Read the type data
    while (*type_start != '\r'){
        type[i] = *type_start;
        type_start+=1;
        i+=1;
    }
    type[i] = '\0';

    // Read the whole message
    while (app_len < header_size + content_length) {
        ssize_t n = read(connection->socket, temp_buffer, sizeof(temp_buffer));
        if (n <= 0) {
            return NULL;
        }

        memcpy(app_buffer + app_len, temp_buffer, n);
        app_len += n;
    }

    char* content_start = app_buffer + header_size;
    Message *message = malloc(sizeof(Message));

    message->length = content_length;
    strcpy(message->type, type);

    if(content_length == 0){
        message->content = NULL;
    } else{
        message->content = malloc(content_length);
        memcpy(message->content, content_start, content_length);
    }
    // app_buffer could contain more than one message, so keep leftover at the buffer
    memcpy(connection->left_buffer, app_buffer + header_size + content_length, app_len - header_size - content_length);
    connection->left_buffer_len = app_len - header_size - content_length;

    return message; 
}

int write_all(int socket, const void *data, size_t length) {
    const unsigned char *pointer = data;
    size_t written = 0;

    while (written < length) {
        // Keep sending the data until all the data are sent
        ssize_t n = write(socket, pointer + written, length - written);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        else if (n == 0) {
            return -1;
        }
        written += n;
    }
    return 0;
}
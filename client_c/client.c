#include <stdio.h>
#include <stdlib.h>
#include "client.h"
#include "../protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>

// Read one whole message from a server
Message* read_one_message(Client* client);
// Write all the data and send it to connected server
int write_all(int socket, const void *data, size_t length);

ClientImplementation* client_init() {
    Client* ptr = malloc(sizeof(Client));
    if (ptr == NULL){
        return NULL;
    }
    // Assign default values 
    ptr->game_id = 0;
    ptr->server_socket = -1;
    ptr->state = 0;
    ptr->left_buffer_len = 0;
    return (ClientImplementation*) ptr;
}

bool client_connect(ClientImplementation* client, const char* addr, uint16_t port, uint32_t game_id) {
    if (client == NULL){
        return false;
    }

    Client* c = (Client*) client;

    struct addrinfo requirements;
    struct addrinfo* result;

    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%u", port);

    memset(&requirements, 0, sizeof(requirements));
    // Use IPv4 address
    requirements.ai_family = AF_INET;
    requirements.ai_socktype = SOCK_STREAM;
    // Create a list of possible addresses from given addr, port_string, requirements
    int status = getaddrinfo(addr, port_string, &requirements, &result);
    if (status != 0) {
        return false;
    }

    int server_socket = -1;
    struct addrinfo* result_pointer;
    // Go through each address getaddrinfo created
    for (result_pointer = result; result_pointer != NULL; result_pointer = result_pointer->ai_next) {
        server_socket = socket(result_pointer->ai_family, result_pointer->ai_socktype, result_pointer->ai_protocol);
        if (server_socket == -1) {
            continue;
        }
        // Connect with a server 
        if (connect(server_socket, result_pointer->ai_addr, result_pointer->ai_addrlen) == 0) {
            break;
        }
        close(server_socket);
        server_socket = -1;
    }
    freeaddrinfo(result);

    if (server_socket == -1) {
        return false;
    }
    c->server_socket = server_socket;
    c->game_id = game_id;
    c->state = 1;

    char header[1000];
    uint32_t content = htonl(game_id);
    // Send game identifier information to the connected server
    snprintf(header, sizeof(header),
    "TYPE:GAME_IDENTIFIER\r\nLENGTH:4\r\n\r\n");
    write_all(c->server_socket, header, strlen(header));
    write_all(c->server_socket, &content, sizeof(content));
    return true;
}

bool client_wait_for_opponent(ClientImplementation* client) {
    if (client == NULL){
        return false;
    }
    Client* c = (Client*) client;
    Message* message = read_one_message(c);
    if (message == NULL){
        return false;
    }
    int expected_length = 5;
    if ((strcmp(message->type, "CONNECTION_STATUS") == 0) && message->length == expected_length && 
        memcmp(message->content, "READY", expected_length) == 0){
        free(message->content);
        free(message);
        return true;
    }
    else if (strcmp(message->type, "FAILURE") == 0){
        free(message->content);
        free(message);
        return false;
    }
    free(message->content);
    free(message);
    return false;
}

int8_t client_send_ships(ClientImplementation* client, const struct Ship (*ships)[4]) {
    if (client == NULL){
        return -1;
    }
    Client* c = (Client*) client;
    char header[1000];
    char content[1000];
    size_t used = 0;
    // Convert 4 ship data to useful format and send it to the server
    for (int i = 0; i<4; i++){
        const char* coordinate = (*ships)[i].coordinate;
        int length = (*ships)[i].length;
        char direction = (*ships)[i].direction == Horizontal ? 'H' : 'V';
        int written = snprintf(content + used, sizeof(content) - used, "%s%s,%d,%c",
                           i == 0 ? "" : "/",
                           coordinate,
                           length,
                           direction);
        used += written;
    }
    snprintf(header, sizeof(header),
         "TYPE:SHIP_PLACEMENT\r\nLENGTH:%zu\r\n\r\n",
         used);

    write_all(c->server_socket, header, strlen(header));
    write_all(c->server_socket, content, used);

    // Receive information about player number from the server
    Message* message = read_one_message(c);
    if (message == NULL){
        return -1;
    }
    if (strcmp(message->type, "PLAYER_NUMBER") != 0) {
        free(message->content);
        free(message);
        return -1;
    }
    if (message->content[0] == '1'){
        free(message->content);
        free(message);
        return 1;
    }
    else{
        free(message->content);
        free(message);
        return 2;
    }
}

TurnResult client_send_move(ClientImplementation* client, const char* coordinate) {
    if (client == NULL){
        return Invalid;
    }
    Client* c = (Client*) client;
    char content[1000];
    // Move is just a coordinate
    snprintf(content, sizeof(content),
         "TYPE:PLAYER_MOVE\r\nLENGTH:%zu\r\n\r\n%s",
         strlen(coordinate), coordinate);
    write_all(c->server_socket, content, strlen(content));

    Message* message = read_one_message(c);
    if (message == NULL) {
        return Invalid;
    }
    if (strcmp(message->type, "TURN_RESULT") != 0){
        free(message->content);
        free(message);
        return Invalid;
    }
    TurnResult result;
    switch (message->content[0]) {
        case '0':
            result = Miss;
            break;
        case '1':
            result = Hit;
            break;
        case '2':
            result = Win;
            break;
        case '4':
            result = Sunk2;
            break;
        case '5':
            result = Sunk3;
            break;
        case '6':
            result = Sunk4;
            break;
        case '7':
            result = Sunk5;
            break;
        default:
            result = Invalid;
            break;
    }
    free(message->content);
    free(message);
    return result;
}

ExtendedTurnResult client_send_move_extended(ClientImplementation* client, const char* coordinate) {
    ExtendedTurnResult result = {
        .length = 0,
        .data = NULL,
    };
    if (client == NULL){
        return result;
    }
    Client* c = (Client*) client;
    char content[1000];

    snprintf(content, sizeof(content),
         "TYPE:PLAYER_MOVE_EXTENDED\r\nLENGTH:%zu\r\n\r\n%s",
         strlen(coordinate), coordinate);
    write_all(c->server_socket, content, strlen(content));

    Message* message = read_one_message(c);
    if (message == NULL) {
        return result;
    }
    if (strcmp(message->type, "TURN_RESULT_EXTENDED") != 0){
        free(message->content);
        free(message);
        return result;
    }
    result.length = message->length;
    result.data = message->content;
    free(message);
    return result;
}

MoveResult client_receive_move(ClientImplementation* client) {
    MoveResult result = {
        .coordinate = NULL,
        .result = Invalid,
    };
    if (client == NULL){
        return result;
    }
    Client* c = (Client*) client;
    Message* message = read_one_message(c);
    if (message == NULL) {
        return result;
    }
    if (strcmp(message->type, "OPPONENT_MOVE") != 0){
        free(message->content);
        free(message);
        return result;
    }
    char coordinate_buffer[20];
    int result_value;
    char content[100];
    // Convert received data to a string
    memcpy(content, message->content, message->length);
    content[message->length] = '\0';
    // Break down the content into coordinate and result
    sscanf(content, "%19[^,],%d",coordinate_buffer, &result_value);

    char* copied_coordinate = malloc(strlen(coordinate_buffer) + 1);
    if (copied_coordinate == NULL) {
        free(message->content);
        free(message);
        return result;
    }
    strcpy(copied_coordinate, coordinate_buffer);

    result.coordinate = copied_coordinate;
    result.result = result_value;
    free(message->content);
    free(message);

    return result;
}

void client_free_extended_result(ExtendedTurnResult result) {
    free((void *) result.data);
    return;
}

void client_free_move_result(MoveResult result) {
    free((void *) result.coordinate);
    return;
}

void client_close(ClientImplementation* client) {
    if (client == NULL){
        return;
    }
    Client* c = (Client*) client;
    close(c->server_socket);
    free(c);
}

Message* read_one_message(Client* client){
    if (client == NULL){
        return NULL;
    }
    int server_socket = client->server_socket;
    char temp_buffer[100];
    char app_buffer[100000]; 
    size_t app_len = 0;
    // Extract the buffer data that was overread last time
    memcpy(app_buffer + app_len, client->left_buffer, client->left_buffer_len);
    app_len += client->left_buffer_len;
    app_buffer[app_len] = '\0';

    // Read the header that ends with \r\n\r\n
    while (strstr(app_buffer, "\r\n\r\n") == NULL) {
        ssize_t n = read(server_socket, temp_buffer, sizeof(temp_buffer));
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
    int length_header_length = 7;
    char* length_start = strstr(app_buffer, "LENGTH:") + length_header_length;
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
    int type_header_length = 5;
    char* type_start = strstr(app_buffer, "TYPE:") + type_header_length;
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
        ssize_t n = read(server_socket, temp_buffer, sizeof(temp_buffer));
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
    memcpy(client->left_buffer, app_buffer + header_size + content_length, app_len - header_size - content_length);
    client->left_buffer_len = app_len - header_size - content_length;

    return message; 
}

int write_all(int socket, const void *data, size_t length) {
    const unsigned char *pointer = data;
    size_t written = 0;
    // Keep sending the data until all the data are sent
    while (written < length) {
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
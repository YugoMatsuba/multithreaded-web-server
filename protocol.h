#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// Data structure for message data transfered
typedef struct Message {
    char type[100];
    uint32_t length;
    unsigned char* content;
} Message;

#endif
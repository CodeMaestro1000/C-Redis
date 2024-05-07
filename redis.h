//
// Created by timothy on 3/29/24.
//

#ifndef REDIS_REDIS_H
#define REDIS_REDIS_H

#endif //REDIS_REDIS_H

// max number of connections that can wait to be accepted.
// Most systems silently limit this number to about 20; you can probably get away with setting it to 5 or 10.

#define MAX_INPUT_CMD_SIZE 1024
#define MAX_MEM_CAPACITY 4096 // how many items can be held in memory at one time

# define SAVE_FILE_NAME "state.rdb"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include "uthash.h"
#include "serde.h"
#include "utils.h"
#include "socket_utils.h"

typedef struct {
    char *key;
    char *value;
    unsigned long exp_milliseconds;
    int expire_list_index; // -1 if expiry was never set.
    int array_size; // 0 for non-arrays, number of items for arrays
    UT_hash_handle hh; /* makes this structure hashable */
} redis_object;


enum redis_exp_type {
    EX,
    PX,
    EXAT,
    PXAT,
    NONE
};

int get_listening_socket(void);
void redis_server_listen(void);
void load_database_from_disk();
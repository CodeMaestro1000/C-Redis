//
// Created by timothy on 4/12/24.
// Redis serialization & deserialization header file
//

#ifndef REDIS_SERDE_H
#define REDIS_SERDE_H

#endif //REDIS_SERDE_H

#define MAX_SIMPLE_STRING_SIZE 128
#define MAX_BULK_STRING_SIZE 2097152 // 2 MB


enum resp_type {
    INTEGER,
    SIMPLE_STRING,
    SIMPLE_ERROR,
    BULK_STRING,
    ARRAY
};

typedef struct {
    enum resp_type data_type;
    void * value;
} resp_message;

// =========================== SER-DE Utilities =================================
unsigned char * serialize(void *, size_t, enum resp_type);
resp_message deserialize_std_type(const unsigned char *);
int deserialize_array(const unsigned char *, resp_message [], size_t);
void clear_message(resp_message *);
void clear_message_array(resp_message [], size_t);
int get_size_from_resp_data(const char *, int);
int deserialize_redis_command(const char *, char *[], size_t);

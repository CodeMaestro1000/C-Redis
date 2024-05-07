//
// Created by timothy on 5/4/24.
//

#include "utils.h"
#include "serde.h"

long get_current_time_ms(){
    long millisecond_val;
    struct timespec time_value;

    clock_gettime(CLOCK_REALTIME, &time_value);
    millisecond_val = (time_value.tv_sec * 1000) + (time_value.tv_nsec / 1e6);
    return millisecond_val;
}

/*
 * Convert an expiration time in millisecond to a unix timestamp
 */
long convert_exp_time_to_timestamp(long exp_time){
    long millisecond_val;

    millisecond_val = get_current_time_ms();
    return millisecond_val + exp_time;
}

/*
 * Gets the number of bytes for a resp message
 */
int get_size_of_resp_simple(const char *message){
    // count how many new line characters have occurred
    int index = 0;
    while(message[index] != '\r' && message[index + 1] != '\n'){
        index++;
        if (index == MAX_SIMPLE_STRING_SIZE)
            return index;
    }

    return index + 2; // for \r and \n
}

int get_size_of_resp_command(const char *message){
    if (message[0] == '+' || message[0] == '-' || message[0] == ':')
        return get_size_of_resp_simple(message);
    if (message[0] == '$'){
        int data_size = get_size_from_resp_data(message, 1);
        int num_size_digits = snprintf(NULL, 0, "%d", data_size);
        return num_size_digits + data_size + 5;
    }
    if (message[0] == '*')
        return 1024; // TODO: Calculate total number of bytes for RESP Array
    return 1024;
}

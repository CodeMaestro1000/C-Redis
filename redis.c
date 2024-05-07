//
// Created by timothy on 3/29/24.
//
#include "redis.h"

redis_object *objects_map = NULL;
int objects_count = 0; // holds the count of items that have been set.
redis_object *timestamped_objects[MAX_MEM_CAPACITY]; // array of pointers
int timed_objects_count = 0;

/*
 * Callback when SET is received.
 */
int handle_set(const char *cmd[], int n_args){
    // TODO: Handle active expiration (every n cycle counts).
    long expiration_timestamp; // can also be of type time_t
    enum redis_exp_type exp_type = NONE; // NONE by default
    long exp_val = 0; // set to 0 by default
    if (objects_count == MAX_MEM_CAPACITY)
        return -1;

    const char *key = cmd[1];
    const char *value = cmd[2];

    if (n_args > 3){ // are there additional arguments?
        if (n_args < 5){ // where is the data for the additional argument?
            fprintf(stderr, "Set Failed: Incomplete argument list");
            return -2;
        }
        if (strcmp(cmd[3], "EX") == 0)
            exp_type = EX;
        else if (strcmp(cmd[3], "PX") == 0)
            exp_type = PX;
        else if (strcmp(cmd[3], "EXAT") == 0)
            exp_type = EXAT;
        else if (strcmp(cmd[3], "PXAT") == 0)
            exp_type = PXAT;
        else exp_type = NONE;

        if (exp_type != NONE){
            exp_val = strtol(cmd[4], (char **)NULL, 10);
            if ((exp_type == PX || exp_type == EX) && (exp_val < 0))
                return -3;
            if (exp_type == EXAT){
                long current_time_ms = get_current_time_ms();
                if ((exp_val * 1000) < current_time_ms)
                    return -4;
                exp_val = exp_val * 1000; // convert to milliseconds
            }
            if (exp_type == PXAT){
                long current_time_ms = get_current_time_ms();
                if ((exp_val) < current_time_ms)
                    return -5;
            }
        }
    }
    redis_object *obj = NULL;
    HASH_FIND_STR(objects_map, key, obj);
    if (obj != NULL){ // key exists, replace
        HASH_DEL(objects_map, obj);
        free(obj); // optional
    }
    obj = (redis_object *) malloc(sizeof *obj);

    // TODO: Remember to free key and value of obj upon deletion.
    obj->key = malloc(sizeof(char) * strlen(key));
    obj->value = malloc(sizeof(char) * strlen(value));
    strcpy(obj->key, key);
    strcpy(obj->value, value);
    if (exp_type == EX) // convert to ms and then timestamp if EX is used
        expiration_timestamp = convert_exp_time_to_timestamp(exp_val * 1000);
    else if (exp_type == PX)
        expiration_timestamp = convert_exp_time_to_timestamp(exp_val);
    else expiration_timestamp = exp_val; // either it is 0 (never expire) or in a timestamp format already (PXAT, EXAT)

    obj->exp_milliseconds = expiration_timestamp;
    obj->array_size = 0;

    if (expiration_timestamp > 0){
        timed_objects_count++;
        timestamped_objects[timed_objects_count - 1] = obj;
        obj->expire_list_index = timed_objects_count - 1;
    }
    else obj->expire_list_index = -1;

    HASH_ADD_STR(objects_map, key, obj);
    objects_count++;
    return 0;
}

int retire_object(redis_object *obj){
    if (obj == NULL)
        return -1;

    if (obj->expire_list_index != -1){ // has expiry
        // copy last into index and set last to NULL
        timestamped_objects[obj->expire_list_index] = timestamped_objects[timed_objects_count - 1];
        timestamped_objects[timed_objects_count - 1]->expire_list_index = obj->expire_list_index;
        timestamped_objects[timed_objects_count - 1] = NULL;
        timed_objects_count--;
    }
    free(obj->key);
    free(obj->value);
    HASH_DEL(objects_map, obj);
    free(obj); // extra step as HASH_DEL should free the object. TODO: Check this in debugger!
    return 0;
}

/*
 * Go through all objects with expiry set and retire expired objects.
 */
void active_objects_expire(){
    long current_timestamp_ms = get_current_time_ms();
    redis_object *obj;

    int exp_count = 0; // todo: For debugging, remove!

    int objects_pointer = timed_objects_count - 1; // look from end of array
    while (objects_pointer >= 0 && timed_objects_count > 0){
        obj = timestamped_objects[objects_pointer];
        if ((current_timestamp_ms > obj->exp_milliseconds) && (obj->exp_milliseconds > 0)){
            exp_count++;
            printf("Found expired data! Key (%s)\n", obj->key);
            retire_object(obj); // this decreases timed_objects_count
        }
        objects_pointer--;
    }
    printf("Found %d expired objects\n", exp_count);
}

/*
 * Callback when GET is received.
 */
redis_object * handle_get(const char *key) {
    redis_object *obj;
    HASH_FIND_STR(objects_map, key, obj);

    long current_timestamp_ms = get_current_time_ms();
    if (obj != NULL && (current_timestamp_ms > obj->exp_milliseconds) && (obj->exp_milliseconds > 0)){
        retire_object(obj);
        return NULL;
    }
    return obj;
}

/*
 * Callback when EXISTS is received.
 */
int handle_exists(const char *cmd[], int n_args){
    int num_existing_keys = 0;
    redis_object *obj;
    const char *key;

    for (int i = 1; i < n_args; i++){
        key = cmd[i]; // works because I'm not changing the data stored in memory, only pointing to a different address
        HASH_FIND_STR(objects_map, key, obj);

        // check if object doesn't exist or is expired
        if (obj != NULL){
            num_existing_keys++;
        }
    }
    return num_existing_keys;
}

/*
 * Callback when DEL is received.
 */
int handle_delete(const char *cmd[], int n_args){
    int num_deleted_keys = 0;
    redis_object *obj;
    const char *key;

    for (int i = 1; i < n_args; i++){
        key = cmd[i];
        HASH_FIND_STR(objects_map, key, obj);

        if (obj != NULL){
            retire_object(obj);
            num_deleted_keys++;
        }
    }
    return num_deleted_keys;
}

/*
 * Callback for INCR or DECR events.
 */
long handle_incr_decr(const char *key, const int value, char **error_msg){
    errno = 0;
    *error_msg = NULL;
    char *end_ptr = NULL;
    redis_object *obj = handle_get(key);
    if (obj == NULL) {
        *error_msg = "Failed: Key does not exist";
        return 0;
    }
    long data = strtol(obj->value, &end_ptr, 10);

    if (obj->value == end_ptr)
        *error_msg = "Failed: No digits found";
    if (errno == ERANGE && data == LONG_MIN)
        *error_msg = "Failed: Underflow"; //underflow occurred;
    if (errno == ERANGE && data == LONG_MAX)
       *error_msg = "Failed: Overflow"; // overflow occurred
    if (errno != 0 && data == 0)
        *error_msg = "Failed: Unspecified err"; // unspecified error

     if (*error_msg  != NULL)
         return 0;

    data += value; // increment or decrement
    size_t data_length = snprintf(NULL, 0, "%li", data);
    free(obj->value);
    char *updated_data = malloc((sizeof(char) * (int)data_length) + 1);
    snprintf(updated_data, data_length + 1, "%li", data);
    strcpy(obj->value, updated_data);
    return data;
}

int handle_left_push(const char *cmd[], int n_args){
    int i, j; // counters
    const char *key = cmd[1];
    unsigned long total_char_size = 0;
    int start_copy_index = 0;
    int num_strings = n_args - 2;
    redis_object *obj;

    for (i = 2; i < n_args; ++i) {
        total_char_size += strlen(cmd[i]);
    }

    HASH_FIND_STR(objects_map, key, obj);
    if (obj == NULL){
        obj = (redis_object *) malloc(sizeof *obj);

        char *value = malloc(sizeof(char) * (total_char_size + num_strings - 1)); // n chars need n - 1 seps

        for (i = n_args - 1; i > 1; i--){
            const char *str_data = cmd[i];
            unsigned long str_data_len = strlen(str_data);
            for (j = 0; j < str_data_len; j++) {
                value[start_copy_index + j] = str_data[j];
            }
            start_copy_index += (int)str_data_len;
            if (i != 2) {
                value[start_copy_index] = '^'; // add sep
                start_copy_index++;
            }
        }

        obj->key = malloc(sizeof(char) * strlen(key));
        strcpy(obj->key, key);
        obj->value = value;
        obj->expire_list_index = -1;
        obj->exp_milliseconds = 0;
        obj->array_size = n_args - 2;
        HASH_ADD_STR(objects_map, key, obj);
    }
    else {
        if (obj->array_size == 0) // not a list
            return -1;

        unsigned long old_length = strlen(obj->value);
        unsigned long new_length = old_length + total_char_size + num_strings - 1;
        char *value = malloc(sizeof(char) * new_length); // space for new string

        //  new additions first
        for (i = n_args - 1; i > 1; i--){
            const char *str_data = cmd[i];
            unsigned long str_data_len = strlen(str_data);
            for (j = 0; j < str_data_len; j++) {
                value[start_copy_index + j] = str_data[j];
            }
            start_copy_index += (int)str_data_len;
            value[start_copy_index] = '^'; // add sep
            start_copy_index++;
        }

        // add existing data
        for (i = 0; i < old_length; i++)
            value[start_copy_index + i] = obj->value[i];

        free(obj->value);
        obj->value = value;
        obj->array_size += n_args - 2;
    }
    return obj->array_size;
}

int handle_right_push(const char *cmd[], int n_args){
    int i, j; // counters
    const char *key = cmd[1];
    unsigned long total_char_size = 0;
    int start_copy_index = 0;
    int num_strings = n_args - 2;
    redis_object *obj;

    for (i = 2; i < n_args; ++i) {
        total_char_size += strlen(cmd[i]);
    }

    HASH_FIND_STR(objects_map, key, obj);
    if (obj == NULL){
        obj = (redis_object *) malloc(sizeof *obj);

        char *value = malloc(sizeof(char) * (total_char_size + num_strings - 1)); // n chars need n - 1 seps

        for (i = 2; i < n_args; i++){
            const char *str_data = cmd[i];
            unsigned long str_data_len = strlen(str_data);
            for (j = 0; j < str_data_len; j++) {
                value[start_copy_index + j] = str_data[j];
            }
            start_copy_index += (int)str_data_len;
            if (i != n_args - 1) {
                value[start_copy_index] = '^'; // add sep
                start_copy_index++;
            }
        }

        obj->key = malloc(sizeof(char) * strlen(key));
        strcpy(obj->key, key);
        obj->value = value;
        obj->expire_list_index = -1;
        obj->exp_milliseconds = 0;
        obj->array_size = n_args - 2;
        HASH_ADD_STR(objects_map, key, obj);
    }
    else {
        if (obj->array_size == 0) // not a list
            return -1;

        unsigned long old_length = strlen(obj->value);
        unsigned long new_length = old_length + total_char_size + num_strings - 1;
        char *value = malloc(sizeof(char) * new_length); // space for new string

        // add existing data first
        for (i = 0; i < old_length; i++)
            value[i] = obj->value[i];

        start_copy_index = (int)old_length;
        value[start_copy_index] = '^';
        start_copy_index++;

        //  insert new additions
        for (i = 2; i < n_args; i++){
            const char *str_data = cmd[i];
            unsigned long str_data_len = strlen(str_data);
            for (j = 0; j < str_data_len; j++) {
                value[start_copy_index + j] = str_data[j];
            }
            start_copy_index += (int)str_data_len;
            if (i != n_args - 1){
                value[start_copy_index] = '^'; // add sep
                start_copy_index++;
            }
        }
        free(obj->value);
        obj->value = value;
        obj->array_size += n_args - 2;
    }
    return obj->array_size;
}

int handle_save(){
    FILE *file_ptr;
    redis_object *obj;

    file_ptr = fopen(SAVE_FILE_NAME, "a");
    if (file_ptr == NULL) {
        fprintf(stderr, "Error opening RDB file");
        return -1;
    }
    for (obj = objects_map; obj != NULL; obj = obj->hh.next) {
        fprintf(
                file_ptr,
                "%s %s %lu %d %d\n",
                obj->key, obj->value, obj->exp_milliseconds, obj->expire_list_index, obj->array_size
                );
    }
    fclose(file_ptr);
    return 0;
}

void load_database_from_disk(){
    FILE *file_ptr;
    char *line = NULL;
    size_t len = 0;
    int load_count = 0;

    char *key_val = malloc(sizeof(char) * MAX_SIMPLE_STRING_SIZE);
    char *value = malloc(sizeof(char) * MAX_SIMPLE_STRING_SIZE);
    unsigned long exp_milliseconds = 0;
    int expire_list_index = -1;
    int array_size = 0;
    redis_object *obj = NULL;

    file_ptr = fopen(SAVE_FILE_NAME, "r");
    if (file_ptr == NULL){
        fprintf(stdout, "No state file found, skipping load.");
        return;
    }

    while ( getline(&line, &len, file_ptr) != -1) {
        obj = (redis_object *) malloc(sizeof *obj);
        sscanf(line, "%s %s %lu %d %d\n", key_val, value, &exp_milliseconds, &expire_list_index, &array_size);
        obj->key = malloc(sizeof(char) * strlen(key_val));
        obj->value = malloc(sizeof(char) * strlen(value));
        strcpy(obj->key, key_val);
        strcpy(obj->value, value);


        obj->exp_milliseconds = exp_milliseconds;
        obj->array_size = array_size;
        if (exp_milliseconds > 0){
            timed_objects_count++;
            timestamped_objects[timed_objects_count - 1] = obj;
            obj->expire_list_index = timed_objects_count - 1;
        }
        else obj->expire_list_index = -1;

        char *key = obj->key; // we need the special name 'key' to hash our data
        // TODO: Test this!
        HASH_ADD_STR(objects_map, key, obj);

        load_count++;
        objects_count++;
    }
    free(key_val);
    free(value);
    fclose(file_ptr);
    printf("Loaded %d objects from disk.\n", load_count);
}

/*
 * Parses an RESP command with possible arguments.
 */
char * handle_resp_command(const char *cmd[], int args){
    // TODO: Handle multiple commands (e.g. command + Arg sent)
    char *response;
    char *resp_response;
    redis_object *obj_data;
    if (strcmp(cmd[0], "PING") == 0){
        response = "PONG";
        resp_response = (char *)serialize(response, strlen(response), SIMPLE_STRING);
        return resp_response;
    }
    if (strcmp(cmd[0], "SET") == 0){
        // pass second and third val
        if (args < 3){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        // if args are three or more
        int ret_val = handle_set(cmd, args);
        if (ret_val == 0){
            response = "OK";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_STRING);
            return resp_response;
        }
        switch (ret_val) { // handle non-zero
            case -1:
                response = "Failed: Max data size reached";
                break;
            case -2:
                response = "Failed: Incomplete argument list";
                break;
            case -3:
                response = "Failed: Expiration value less than zero";
                break;
            case -4:
                response = "Failed: Expiration timestamp (in seconds) before current time";
                break;
            case -5:
                response = "Failed: Expiration timestamp (in milliseconds) before current time";
                break;
            default:
                response = "Failed: Unknown Error";
        }
        resp_response = (char *)serialize(response, strlen(response), SIMPLE_STRING);
        return resp_response;
    }
    if (strcmp(cmd[0], "GET") == 0){
        if (args < 2){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        obj_data = handle_get(cmd[1]);
        if (obj_data == NULL){
            response = "Failed: Key does not exist";
            resp_response = (char *) serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        resp_response = (char *) serialize(obj_data->value, strlen(obj_data->value), BULK_STRING); // n_bytes - 1 to exclude null terminator
        return resp_response;
    }
    if (strcmp(cmd[0], "EXISTS") == 0){
        if (args < 2){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        int exists_data = handle_exists(cmd, args);
        resp_response = (char *) serialize(&exists_data, 0, INTEGER);
        return resp_response;
    }
    if (strcmp(cmd[0], "DEL") == 0){
        if (args < 2){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        int delete_data = handle_delete(cmd, args);
        resp_response = (char *) serialize(&delete_data, 0, INTEGER);
        return resp_response;
    }
    if (strcmp(cmd[0], "INCR") == 0 || strcmp(cmd[0], "DECR") == 0){
        if (args < 2){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        long result;
        int value;
        char *error_message = "";

        if (strcmp(cmd[0], "INCR") == 0)
            value = 1;
        else value = -1;

        result = handle_incr_decr(cmd[1], value, &error_message);
        if (error_message != NULL){
            resp_response = (char *)serialize(error_message, strlen(error_message), SIMPLE_ERROR);
        }
        else resp_response = (char *) serialize(&result, 0, INTEGER);
        return resp_response;
    }
    if ((strcmp(cmd[0], "LPUSH") == 0) || (strcmp(cmd[0], "RPUSH") == 0) ){
        if (args < 3){
            response = "Failed: Incomplete argument list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
            return resp_response;
        }
        int value;
        if (strcmp(cmd[0], "LPUSH") == 0)
            value = handle_left_push(cmd, args);
        else value = handle_right_push(cmd, args);

        if (value < 0){
            response = "Failed: Value of key not a list";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
        }
        else resp_response = (char *) serialize(&value, 0, INTEGER);
        return resp_response;
    }
    if (strcmp(cmd[0], "SAVE") == 0){
        int value = handle_save();
        if (value == -1){
            response = "Failed: Error saving to file";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
        }
        else {
            response = "OK";
            resp_response = (char *)serialize(response, strlen(response), SIMPLE_STRING);
        }
        return resp_response;
    }

    response = "Unknown Command";
    resp_response = (char *)serialize(response, strlen(response), SIMPLE_ERROR);
    return resp_response;
}

/*
 * Handles a new connection on the listening socket
 */
int handle_new_connection(const int *listener){
    int client_socket;
    struct sockaddr_storage client_conn_addr; // hold struct sockaddr of type IPv4 or IPv6.
    socklen_t client_addr_size;
    ip_details client_ip_info;
    char client_ip_str[INET6_ADDRSTRLEN];

    client_addr_size = sizeof client_conn_addr;
    // accept only puts X amount of bytes into client_conn_addr, hence why we need to know the size
    client_socket = accept(*listener, (struct sockaddr *)&client_conn_addr, &client_addr_size);
    if (client_socket == -1){
        perror("Error accepting new connection");
        return -1;
    }
    get_ip_details((struct sockaddr *)&client_conn_addr, &client_ip_info);
    inet_ntop(client_conn_addr.ss_family, client_ip_info.ip_address, client_ip_str, INET6_ADDRSTRLEN);
    printf("Redis server: New connection from %s on socket %d\n", client_ip_str, client_socket);
    return client_socket;
}

void handle_message(char *message_buffer, const int *sender_socket){
    char *resp_response;
    int ser_de_error = 0; // error if there's a serialization/deserialization problem

    if (message_buffer[0] == '*'){
        int arr_size = get_size_from_resp_data(message_buffer, 1);
        char *cmd_string[(int)arr_size];

        int num_cmds = deserialize_redis_command(message_buffer, cmd_string, arr_size);

        if (num_cmds != arr_size) {
            fprintf(stderr,
                    "Redis server: Deserialization failed. At least one invalid RESP message received.\n"
            );
            resp_response = (char *)serialize("Error", strlen("Error"), SIMPLE_STRING);
            ser_de_error = 1;
        }
        else resp_response = handle_resp_command((const char **) cmd_string, arr_size);

        int packet_length = get_size_of_resp_command(resp_response);
        int num_bytes_sent = sendall(*sender_socket, resp_response, &packet_length);
        if (num_bytes_sent == -1)
            perror("Error sending to client");
        free(resp_response);

        if (!ser_de_error){ // no need to free anything
            for (int k = 0; k < arr_size; k++)
                free(cmd_string[k]);
        }
    }
}

void redis_server_listen() {
    int listener;
    int client_socket;

    char message_buffer[MAX_INPUT_CMD_SIZE];

    int sockets_count = 0;
    int num_sockets_allowed = 5; // start-off with 5 maximum connections

    // allocate sizeof (1 pollfd * num_sockets_allowed) bytes
    struct pollfd *sockets_arr = malloc(sizeof *sockets_arr * num_sockets_allowed);

    listener = get_listening_socket();

    if (listener == -1) {
        fprintf(stderr, "Redis server: error getting listening socket\n");
        exit(1);
    }
    printf("Redis server: (127.0.0.1) listening on port %s\n", REDIS_PORT);
    // Add the listener to set
    sockets_arr[0].fd = listener;
    sockets_arr[0].events = POLLIN; // Report ready to read on incoming connection

    sockets_count = 1; // For the listener

    load_database_from_disk();

    for(;;) {
        // sleep until for 10 secs or until there is data to be received. We use the poll() function
        // poll() hands over sleeping and waiting for data to the OS. Maybe at the OS level this is handled by
        // interrupts. I'm not sure!
        int poll_count = poll(sockets_arr, sockets_count, 20000); // -1 means never timeout
        if (poll_count == -1) {
            perror("poll error"); // notice we use perror for os level function calls
            exit(1);
        }

        for (int i = 0; i < sockets_count; i++) {
            // Guard clause: If the event is not POLLIN (data to be received or read), move to next socket.
            if (!(sockets_arr[i].revents & POLLIN))
                continue;

            if (sockets_arr[i].fd == listener) {
                // if the listener is the socket ready to receive data, then there is a new connection.

                // handle callback
                client_socket = handle_new_connection(&listener);
                if (client_socket != -1)
                    add_socket(&sockets_arr, client_socket, &sockets_count, &num_sockets_allowed);
            }
            else { // if ready socket is not the listener, it is a client that sent out data
                int num_bytes_recv = (int)recv(sockets_arr[i].fd, message_buffer, sizeof message_buffer, 0);

                int sender_socket = sockets_arr[i].fd;
                if (num_bytes_recv == 0) {
                    printf("Redis server: socket %d hung up\n", sender_socket);
                    close(sender_socket);
                    remove_socket(sockets_arr, i, &sockets_count);
                }
                    // repetition here to prevent too much nesting
                else if (num_bytes_recv < 0) {
                    perror("Error receiving from socket");
                    close(sender_socket);
                    remove_socket(sockets_arr, i, &sockets_count);
                }
                else { // there's actual data received
                    printf("Redis server: Received from socket %d <", sender_socket);
                    for (int j = 0; j < num_bytes_recv; j++){
                        if (message_buffer[j] == '\r')
                            printf("/r");
                        else if (message_buffer[j] == '\n')
                            printf("/n");
                        else
                            printf("%c", message_buffer[j]);
                    }
                    printf(">\n");

                    // REDIS SER-DE here
                    handle_message(message_buffer, &sender_socket);
                    memset(message_buffer, 0, MAX_INPUT_CMD_SIZE);
                }
            } // end when ready socket is a client

        } // end sockets iteration

        // remove expired objects after every 10 secs or after a new connection has been established.
        active_objects_expire();
    } // end loop-forever
} // end server listening function

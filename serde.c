//
// Created by timothy on 4/12/24.
// Serialization & deserialization source file
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "serde.h"

/*
 * Converts an int to an RESP integer.
 */
unsigned char * int_to_resp_str(const int *val_ptr){
    int data = *val_ptr;
    size_t data_length = snprintf(NULL, 0, "%d", *val_ptr); // get num of chars needed to rep str
    size_t str_len = data_length + 3;

    char *str_rep = malloc(sizeof(char) * data_length); // hold actual string representation of number
    unsigned char *out_str = malloc(sizeof (unsigned char) * str_len); // output buffer

    snprintf(str_rep, data_length + 1, "%d", data); // convert int to str
    out_str[0] = ':';
    for (int i = 1; i <= data_length; i++){
        out_str[i] = str_rep[i - 1]; // copy str repr to output
    }
    out_str[data_length + 1] = '\r';
    out_str[data_length + 2] = '\n';
    free(str_rep); // free resources
    return out_str; // will be freed once this buffer is no longer needed by the caller.
}

unsigned char * str_to_simple_resp(const char *val_ptr, size_t len, int error){
    if (len > MAX_SIMPLE_STRING_SIZE){
        return NULL;
    }
    size_t str_len = len + 3;
    unsigned char *out_str = malloc(sizeof (unsigned char) * str_len);
    if(error)
        out_str[0] = '-';
    else out_str[0] = '+';
    for (int i = 1; i <= len; i++) {
        // simple resp must not contain \r or \n
        if (val_ptr[i] == '\r' || val_ptr[i] == '\n'){
            return NULL;
        }
        out_str[i] = val_ptr[i - 1];
    }
    out_str[len + 1] = '\r';
    out_str[len + 2] = '\n';
    return out_str;
}

/*
 * Converts a str to an RESP simple str.
 *
 * Args:
 * val_ptr - pointer to the start of the string to be serialized
 * len - length of the string to be serialized
 *
 * Returns:
 * Pointer to serialized byte array or NULL pointer.
 *
 * Returns NULL if len > MAX_SIMPLE_STRING_SIZE (128 bytes) defined in redis.h or if string contains
 * \r or \n characters.
 */
unsigned char * str_to_resp_simple_str(const char *val_ptr, size_t len){
    return str_to_simple_resp(val_ptr, len, 0); // not an error
}

// TODO: Add error serializatioin and Deserialization
unsigned char * str_to_resp_simple_err(const char *val_ptr, size_t len){
    return str_to_simple_resp(val_ptr, len, 0); // not an error
}

/*
 * Converts a str to an RESP bulk str.
 */
unsigned char * str_to_resp_bulk_str(const char *val_ptr, size_t len){
    if (len > MAX_BULK_STRING_SIZE){
        return NULL;
    }
    int num_size_digits = snprintf(NULL, 0, "%d", (int)len); // find num chars to rep len
    size_t str_len = len + num_size_digits + 5;
    int shift_pos = num_size_digits + 1; // $ + len of chars for size
    unsigned char *out_str = malloc(sizeof (unsigned char) * str_len);
    char *size_str_rep = malloc(num_size_digits + 1);
    snprintf(size_str_rep, num_size_digits + 1, "%d", (int)len);
    out_str[0] = '$';
//    out_str[1] = len;
    for (int i = 0; i < num_size_digits; i++)
        out_str[i + 1] = size_str_rep[i];

    free(size_str_rep);
    out_str[shift_pos] = '\r';
    out_str[shift_pos + 1] = '\n';
    for (int i = 0; i < len; i++)
        out_str[shift_pos + 2 + i] = val_ptr[i];
    out_str[shift_pos + 2 + len] = '\r';
    out_str[shift_pos + 3 + len]  = '\n';
    return out_str;
}

/*
 * Get the number of characters needed to represent a standard type in RESP format.
 */
int get_n_chars(void *data, enum resp_type d_type){
    int data_length = 0;
    if (d_type == INTEGER){
        size_t length = snprintf(NULL, 0, "%d", *(int *)data); // get num of chars needed to rep str
        data_length = (int)length + 3;
        return data_length;
    }
    if (d_type == SIMPLE_STRING || d_type == SIMPLE_ERROR){
        size_t length = strlen((char *)data);
        data_length = (int)length + 3;
        return  data_length;
    }
    if (d_type == BULK_STRING){
        size_t length = strlen((char *)data);
        data_length = (int)length + 6;
        return  data_length;
    }
    return data_length;
}

/*
 * Gets the number of bytes needed to store an RESP type.
 */
size_t get_size(const resp_message *msg){
    switch (msg->data_type) {
        case INTEGER:
            return sizeof(char) * get_n_chars(msg->value, INTEGER);
        case SIMPLE_STRING:
            return sizeof(char) * get_n_chars(msg->value, SIMPLE_STRING);
        case SIMPLE_ERROR:
            return sizeof(char) * get_n_chars(msg->value, SIMPLE_ERROR);
        case BULK_STRING:
            return sizeof(char) * get_n_chars(msg->value, BULK_STRING);
        default:
            return (size_t) 1;
    }
}

/*
 * Serializes an array of RESP messages. Since the data type must be known beforehand, you must construct
 * an RESP message array and pass it by reference to this function.
 */
unsigned char * serialize_resp_array(const resp_message msg[], size_t array_size){
//    *<number-of-elements>\r\n<element-1>...<element-n>
    // allocate the total number of bytes for each data type
    size_t total_size = 0;
    size_t str_len;
    int start_pos;
    int total_chars;
    unsigned char *output, *data_val;

    // get size needed to store all array contents
    for (int i = 0; i < array_size; i++){
        total_size += get_size(&msg[i]);
    }
    output = malloc(total_size + 4);
    output[0] = '*';
    output[1] = array_size;
    output[2] = '\r';
    output[3] = '\n';

    // indicate starting position for insert
    start_pos = 4;
    for (int i = 0; i < array_size; i++){
        switch (msg[i].data_type) {
            case INTEGER:
                total_chars = get_n_chars(msg[i].value, INTEGER);
                data_val = int_to_resp_str((int *)msg[i].value);
                for (int j = 0; j < total_chars; j++)
                    output[j + start_pos] = data_val[j];
                start_pos += total_chars;
                free(data_val); // free memory allocated by int_to_resp_str
                break;
            case SIMPLE_STRING:
                str_len = strlen((char *)msg[i].value);
                total_chars = (int)str_len + 3;
                data_val = str_to_resp_simple_str((char *)msg[i].value, str_len);
                if (data_val == NULL)
                    return NULL;
                for (int j = 0; j < total_chars; j++)
                    output[j + start_pos] = data_val[j];
                start_pos += total_chars;
                free(data_val); // free memory allocated by str_to_resp_simple_str
                break;
            case SIMPLE_ERROR:
                str_len = strlen((char *)msg[i].value);
                total_chars = (int)str_len + 3;
                data_val = str_to_resp_simple_err((char *)msg[i].value, str_len);
                if (data_val == NULL)
                    return NULL;
                for (int j = 0; j < total_chars; j++)
                    output[j + start_pos] = data_val[j];
                start_pos += total_chars;
                free(data_val); // free memory allocated by str_to_resp_simple_str
                break;
            case BULK_STRING:
//                str_len = get_n_chars(msg[i].value, BULK_STRING);
                str_len = strlen((char *)msg[i].value);
                total_chars = (int)str_len + 6;
                data_val = str_to_resp_bulk_str((char *)msg[i].value, str_len);
                if (data_val == NULL)
                    return NULL;
                for (int j = 0; j < total_chars; j++)
                    output[j + start_pos] = data_val[j];
                start_pos += total_chars;
                free(data_val); // free memory allocated by str_to_resp_simple_str
                break;
            default:
                free(output);
                return NULL;
        }
    }
    return output;
}

/*
 * Takes a standard type and converts it to a RESP type.
 *
 * Args:
 * addr - the pointer to the standard object to be serialized. Must be type cast to match d_type
 * len - the length of the standard object to be serialized. This value is ignored if d_type is an INTEGER
 * d_type - the resp type the standard object should be serialized to. Might obtain unexpected behaviour if an incompatible
 * type is passed in addr.
 *
 * Warning:
 * Simple strings can only be 128 characters long. Use Bulk strings for longer data.
 */
unsigned char * serialize (void *addr, size_t len, enum resp_type d_type){
    // do something
//    resp_message msg;
    unsigned char * serialized_output;

    switch (d_type) {
        case INTEGER:
            serialized_output = int_to_resp_str((int *) addr);
            break;
        case SIMPLE_STRING:
            serialized_output = str_to_resp_simple_str((char *) addr, len);
            break;
        case SIMPLE_ERROR:
            serialized_output = str_to_resp_simple_err((char *) addr, len);
            break;
        case BULK_STRING:
            serialized_output = str_to_resp_bulk_str((char *) addr, len);
            break;
        case ARRAY:
            serialized_output = serialize_resp_array((resp_message *) addr, len);
            break;
        default:
            serialized_output = NULL;
    }
    return serialized_output;
}

/*
 * Converts an RESP integer into a standard integer. Returns a pointer to the deserialized integer
 */
int * deserialize_int(const unsigned char * msg_array){
    // extract only the number part of the RESP data
    // add a terminator to make it a C string
    // convert with the strtol and return pointer to converted value

    int index = 1;
    int *output = malloc(sizeof(int));
    size_t max_str_length = snprintf(NULL, 0, "%d", INT_MAX); // get the num of chars needed to rep INT_MAX
    // look forward till the terminator \r
    while(msg_array[index] != '\r' & index < max_str_length)
        index++;
    if (msg_array[index] != '\r') { // no \r before the max_str_length was reached.
        return NULL;
    }
    char *num_str = malloc(sizeof(char) * index);
    for (int i = 1; i < index; i++)
        num_str[i - 1] = (char)msg_array[i];
    num_str[index] = '\0';

    *output = (int) strtol(num_str, (char **)NULL, 10);
    return output;
}

/*
 * Converts an RESP simple string to a C-style string (character array that is null terminated).
 */
char * deserialize_simple_str(const unsigned char * msg_array){
    int index = 1;
    // look forward till the terminator \r
    while(msg_array[index] != '\r' & index < MAX_SIMPLE_STRING_SIZE)
        index++;
    if (msg_array[index] != '\r') { // no \r before the max_str_length was reached.
        return NULL;
    }
    char *output = malloc(sizeof(char) * index);
    for (int i = 1; i < index; i++)
        output[i - 1] = (char)msg_array[i];
    output[index] = '\0';
    return output;
}

/*
 * Gets the integer representation of the characters (or bytes) that represent the size of a bulk string or array
 */
int get_size_from_resp_data(const char *msg_array, int start_index){
    int index = start_index;
    while (msg_array[index] != '\r' && msg_array[index + 1] != '\n')
        index++;
    char str_len[index - start_index + 1];
    for (int i = start_index; i < index; i++)
        str_len[i - start_index] = msg_array[i];
    str_len[index - 1] = '\0';
    return (int)strtol(str_len, NULL, 10);
}


/*
 * Deserializes an RESP bulk string to a C-style string (character array that is null terminated).
 */
char * deserialize_bulk_str(const unsigned char *msg_array){
    // $<length>\r\n<data>\r\n
    int str_len = get_size_from_resp_data((char *)msg_array, 1);
    int num_size_digits = snprintf(NULL, 0, "%d", str_len);
    int shift_pos = num_size_digits + 3;
    if (str_len > MAX_BULK_STRING_SIZE)
        return NULL;
    char *out_str = malloc(sizeof(char) * (str_len + 1));
    for (int i = 0; i < str_len; i++)
        out_str[i] = (char)msg_array[i + shift_pos];
    out_str[str_len] = '\0';
    return out_str;
}

/*
 * Gets the position of the \r\n terminated RESP data
 */
int get_end_index(int start_index, const unsigned char *data){
    int index = start_index;
    while(data[index] != '\r'){
        index++;
    }
    if (data[index + 1] != '\n')
        return -1;
    return index + 1;
}

/*
 * Copies bytes from start until (and including end) from src into dest. The position of each char in dest is shifted
 * left by n positions where n is the value of start. Hence, end cannot be less than start.
 */
int copy_byte_array(const unsigned char *src, unsigned char *dest, int start, int end){
    if (end < start)
        return 0;
    int str_length = end - start;
    for (int i = 0; i <= str_length; i++) //
        dest[i] = src[i + start];
    return 1;
}

/*
 * Deserializes an RESP array into an array of resp_messages. This is because we want to know the data type of each item
 * after deserialization.
 */
int deserialize_array(const unsigned char *msg_array, resp_message out[], size_t array_size){
    //*<size>\r\n<elem_1>....<elem_n>
    int data_count = 0;
    int success_ops = 0; // number of successful deserializations
    int start_pos = 4;

    int copy_success;

    // find all data within array_size
    while (data_count < (int)array_size){
        char data_type = (char)msg_array[start_pos];
        if (data_type == ':'){ // integer
            int end_pos = get_end_index(start_pos, msg_array);
            if (end_pos <= start_pos){ // also works if -1 is returned from get_end_index
                out[data_count].data_type = INTEGER;
                out[data_count].value = NULL;
                continue;
            }
            unsigned char int_data[end_pos - start_pos + 1];
            copy_success = copy_byte_array(msg_array, int_data, start_pos, end_pos);
            if (!copy_success)
                continue;
            int *int_val = deserialize_int(int_data);
            out[data_count].data_type = INTEGER;
            out[data_count].value = int_val;
            start_pos = end_pos + 1;
            success_ops++;
        }
        else if (data_type == '+' || data_type == '-'){
            int end_pos = get_end_index(start_pos, msg_array);
            if (end_pos <= start_pos){ // also works if -1 is returned from get_end_index
                if (data_type == '+')
                    out[data_count].data_type = SIMPLE_STRING;
                else out[data_count].data_type = SIMPLE_ERROR;
                out[data_count].value = NULL;
                continue;
            }
            unsigned char simple_str_data[end_pos - start_pos + 1];
            copy_success = copy_byte_array(msg_array, simple_str_data, start_pos, end_pos);
            if (!copy_success)
                continue;
            char *str_val = deserialize_simple_str(simple_str_data);
            if (data_type == '+')
                out[data_count].data_type = SIMPLE_STRING;
            else out[data_count].data_type = SIMPLE_ERROR;
            out[data_count].value = str_val;
            start_pos = end_pos + 1;
            success_ops++;
        }
        else if (data_type == '$'){
            // TODO: Find standard to use (char vs unsigned char)
            int str_length = get_size_from_resp_data((char *)msg_array, start_pos + 1);
            int num_size_digits = (int)snprintf(NULL, 0, "%d", str_length); // get num of chars needed

            // we need the $, two \r, two \n and the num of chars needed to rep the length
            //$length\r\ndata\r\n
            unsigned char bulk_str_data[str_length + 5 + num_size_digits];
            int end_pos = start_pos + str_length + 4 + num_size_digits; // we're already at the $
            copy_success = copy_byte_array(msg_array, bulk_str_data, start_pos, end_pos);
            if (!copy_success)
                continue;
            char *bulk_str_val = deserialize_bulk_str(bulk_str_data);
            out[data_count].data_type = BULK_STRING;
            out[data_count].value = bulk_str_val;
            start_pos = end_pos + 1;
            success_ops++;
        }
        data_count++;
    }
    return success_ops;
}

/*
 * Takes RESP data and converts it to a standard type.
 *
 * Args
 * array - byte array containing RESP data
 * Warning - the value in the resp_message will be NULL if deserialization fails.
 */
resp_message deserialize_std_type(const unsigned char *array){
    char type_symbol = (char)array[0];
    resp_message out;
    // can't use switch because of dynamic pointer creation.
    if (type_symbol == ':'){
        int *val = deserialize_int(array);
        out.data_type = INTEGER;
        out.value = val;
    }
    else if (type_symbol == '+' || type_symbol == '-') {
        char *val = deserialize_simple_str(array);
        if (type_symbol == '+')
            out.data_type = SIMPLE_STRING;
        else out.data_type = SIMPLE_ERROR;
        out.value = val;
    }
    else if (type_symbol == '$'){
        char *val = deserialize_bulk_str(array);
        out.data_type = BULK_STRING;
        out.value = val;
    }
    else {
        exit(1);
    }
    return out;
}

int deserialize_redis_command(const char *array_in, char *arr_out[], size_t arr_size){
    resp_message out[(int)arr_size];
    int num_cmds;
    // commands should always be strings.
    num_cmds = deserialize_array((unsigned char *)array_in, out, arr_size);

    for (int i = 0; i < arr_size; i++){
        arr_out[i] = (char *)out[i].value;
    }
    return num_cmds;
}

/*
 * release memory allocated from deserialization of standard types.
 */
void clear_message(resp_message *msg){
    free(msg->value);
}

void clear_message_array(resp_message msg_array[], size_t array_size){
    for (int i = 0; i < array_size; i++)
        clear_message(&msg_array[i]);
}

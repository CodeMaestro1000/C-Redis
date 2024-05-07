//
// Created by timothy on 5/4/24.
//

#ifndef REDIS_SOCKET_UTILS_H
#define REDIS_SOCKET_UTILS_H

#endif //REDIS_SOCKET_UTILS_H

#define REDIS_PORT "6379"
#define CONN_REQUESTS_QUEUE_SIZE 10

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>


typedef struct {
    char *ip_version;
    void *ip_address;
} ip_details;

int get_listening_socket();
void add_socket(struct pollfd *socket_list[], int socket, int *sockets_count, int *num_sockets_allowed);
void remove_socket(struct pollfd socket_list[], int socket_idx, int *sockets_count);
void get_ip_details(struct sockaddr *ip_input, ip_details *ip_out);
int sendall(int socket, char *message, int *len);
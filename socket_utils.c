//
// Created by timothy on 5/4/24.
//

#include "socket_utils.h"

int get_listening_socket() {
    int listener;
    int yes = 1; // needed to set socket options. not sure  why
    struct addrinfo address_criteria; // specify criteria to limit the set of socket addresses returned by getaddrinfo()
    struct addrinfo *address_list, *ip_addr_ptr;

    memset(&address_criteria, 0, sizeof address_criteria);
    address_criteria.ai_family = AF_UNSPEC; // IPv4 or IPv6
    address_criteria.ai_socktype = SOCK_STREAM;
    address_criteria.ai_flags = AI_PASSIVE; // use the IP of the host machine

    int ret_val = getaddrinfo(NULL, REDIS_PORT, &address_criteria, &address_list);
    if (ret_val != 0){
        fprintf(stderr, "Couldn't get IP address info of host. Error: %s\n", gai_strerror(ret_val));
        exit(1);
    }

    // iterate through address, bind the socket and then create a listener
    for (ip_addr_ptr = address_list; ip_addr_ptr != NULL; ip_addr_ptr = ip_addr_ptr->ai_next) {
        listener = socket(ip_addr_ptr->ai_family, ip_addr_ptr->ai_socktype, ip_addr_ptr->ai_protocol);
        if (listener < 0)
            continue;

        // use SO_REUSEADDR to get around “Address already in use” when restarting after a crash.
        // SO_REUSEADDR gives us the option to bind() to a port, unless there is an active listening socket bound to the port already.
        int sock_opts = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (sock_opts == -1){
            perror("Error setting socket options. Exiting.\n");
            exit(1);
        }

        int bind_val = bind(listener, ip_addr_ptr->ai_addr, ip_addr_ptr->ai_addrlen);
        if (bind_val < 0) { // close the socket (fd) if it fails to bind
            close(listener);
            continue;
        }
        break;
    }
    freeaddrinfo(address_list);
    if (ip_addr_ptr == NULL)
        return -1;

    if (listen(listener, CONN_REQUESTS_QUEUE_SIZE) == -1) // return -1 if listening fails to start
        return -1;

    return listener;
}

void add_socket(struct pollfd *socket_list[], int socket, int *sockets_count, int *num_sockets_allowed)
{
    // If we don't have room, add more space in the pfds array
    if (*sockets_count == *num_sockets_allowed) {
        *num_sockets_allowed *= 2; // Double it

        // re-allocate more space for entire socket_list array
        *socket_list = realloc(*socket_list, sizeof(**socket_list) * (*num_sockets_allowed));
    }

    (*socket_list)[*sockets_count].fd = socket;
    (*socket_list)[*sockets_count].events = POLLIN; // Check ready-to-read

    (*sockets_count)++;
}

// Remove an index from the set
void remove_socket(struct pollfd socket_list[], int socket_idx, int *sockets_count)
{
    // Copy the one from the end over this one
    socket_list[socket_idx] = socket_list[*sockets_count-1];

    (*sockets_count)--;
}

void get_ip_details(struct sockaddr *ip_input, ip_details *ip_out){
    if (ip_input->sa_family == AF_INET){
        // get the IPv4 address
        ip_out->ip_address = &((struct sockaddr_in *)ip_input)->sin_addr;
        ip_out->ip_version = "IPv4";
    }
    else if (ip_input->sa_family == AF_INET6){
        ip_out->ip_address = &((struct sockaddr_in6 *)ip_input)->sin6_addr;
        ip_out->ip_version = "IPv6";
    }
    else {
        ip_out->ip_version = "";
        ip_out->ip_address = NULL;
    }
}

/*
 * Attempt to send all bytes. From Beej's guide
 */
int sendall(int socket, char *message, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytes_left = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = (int)send(socket, message+total, bytes_left, 0);
        if (n == -1) { break; }
        total += n;
        bytes_left -= n;
    }

    *len = total; // return number actually sent here

    return n==-1?-1:0; // return -1 on failure, 0 on success
}

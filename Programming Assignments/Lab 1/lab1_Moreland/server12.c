/*
** server12.c -- a TCP calculator server for Lab 1.2
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdint.h>

#define PORT "10020"  // The port users will be connecting to
#define BACKLOG 10	  // How many pending connections queue will hold

// --- Protocol Structures ---
#pragma pack(push, 1)
struct RequestMessage {
    uint8_t op_code;
    uint32_t op_a;
    uint32_t op_b;
};

struct ResponseMessage {
    uint8_t op_code;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t answer;
    uint8_t is_valid;
};
#pragma pack(pop)

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        // --- Handle client request ---
        struct RequestMessage req;
        if (recv(new_fd, &req, sizeof(req), 0) == -1) {
            perror("recv");
        } else {
            // Convert operands from network to host byte order
            req.op_a = ntohl(req.op_a);
            req.op_b = ntohl(req.op_b);

            struct ResponseMessage res;
            res.op_code = req.op_code;
            res.op_a = req.op_a;
            res.op_b = req.op_b;
            res.is_valid = 1; // Assume valid initially

            // Perform calculation
            switch (req.op_code) {
                case '+': res.answer = req.op_a + req.op_b; break;
                case '-': res.answer = req.op_a - req.op_b; break;
                case 'x': res.answer = req.op_a * req.op_b; break;
                case '/':
                    if (req.op_b == 0) {
                        res.is_valid = 2; // Invalid
                        res.answer = 0;
                    } else {
                        res.answer = req.op_a / req.op_b;
                    }
                    break;
                default:
                    res.is_valid = 2; // Invalid op
                    res.answer = 0;
                    break;
            }
            
            printf("server: received %u %c %u, sending result %u\n", 
                   req.op_a, req.op_code, req.op_b, res.answer);

            // Convert response fields to network byte order
            res.op_a = htonl(res.op_a);
            res.op_b = htonl(res.op_b);
            res.answer = htonl(res.answer);
            
            if (send(new_fd, &res, sizeof(res), 0) == -1) {
                perror("send");
            }
        }
        
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}

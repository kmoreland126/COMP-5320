/*
** client12.c -- a TCP calculator client for Lab 1.2
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>

#define PORT "10020" // the port client will be connecting to 

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

int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    if (argc != 5) {
        fprintf(stderr, "usage: client <hostname> <num1> <operator> <num2>\n");
        exit(1);
    }
    
    // --- Argument Parsing ---
    char* hostname = argv[1];
    uint32_t num1 = strtoul(argv[2], NULL, 10);
    char op = argv[3][0];
    uint32_t num2 = strtoul(argv[4], NULL, 10);

    if (strlen(argv[3]) != 1 || (op != '+' && op != '-' && op != 'x' && op != '/')) {
        fprintf(stderr, "Error: Invalid operator. Must be '+', '-', 'x', or '/'.\n");
        exit(1);
    }
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(hostname, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure

    // --- Prepare and Send Request ---
    struct RequestMessage req;
    req.op_a = htonl(num1); // Convert to network byte order
    req.op_b = htonl(num2); // Convert to network byte order
    req.op_code = op;
    
    if (send(sockfd, &req, sizeof(req), 0) == -1) {
        perror("send");
        exit(1);
    }
    printf("client: sent request: %u %c %u\n", num1, op, num2);

    // --- Receive and Process Response ---
    struct ResponseMessage res;
    if (recv(sockfd, &res, sizeof(res), 0) == -1) {
        perror("recv");
        exit(1);
    }

    // Convert response fields from network to host byte order
    res.op_a = ntohl(res.op_a);
    res.op_b = ntohl(res.op_b);
    res.answer = ntohl(res.answer);

    // --- Display Result ---
    if (res.is_valid == 1) {
        printf("Server response: %u %c %u = %u\n", res.op_a, res.op_code, res.op_b, res.answer);
    } else {
        fprintf(stderr, "Server response: Error, the operation could not be completed (e.g., division by zero).\n");
    }

    close(sockfd);
    return 0;
}

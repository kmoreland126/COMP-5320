/*
** client11c.c -- a UDP client with two processes for Lab 1.1
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdint.h>
#include <limits.h>

#define SERVERPORT "10010"
#define NUM_PACKETS 10000

#pragma pack(push, 1)
struct PktHeader {
    uint16_t total_len;
    uint32_t seq_num;
    uint64_t timestamp;
};
#pragma pack(pop)

int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    pid_t pid;

    if (argc != 2) {
        fprintf(stderr, "usage: client hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // Find a valid address to create a socket
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to create socket\n");
        return 2;
    }

    // Create two processes
    pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        // --- Child Process: Sender ---
        printf("Sender (child) process created.\n");
        for (uint32_t i = 1; i <= NUM_PACKETS; i++) {
            char num_str[20];
            sprintf(num_str, "%u", i);
            uint16_t str_len = strlen(num_str);

            struct PktHeader header;
            header.total_len = sizeof(struct PktHeader) + str_len;
            header.seq_num = i;

            struct timeval tv;
            gettimeofday(&tv, NULL);
            header.timestamp = (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;

            // Convert to network byte order
            header.total_len = htons(header.total_len);
            header.seq_num = htonl(header.seq_num);
            // header.timestamp does not need conversion for RTT calculation on the same machine

            char send_buf[sizeof(struct PktHeader) + str_len];
            memcpy(send_buf, &header, sizeof(struct PktHeader));
            memcpy(send_buf + sizeof(struct PktHeader), num_str, str_len);

            if (sendto(sockfd, send_buf, sizeof(send_buf), 0, p->ai_addr, p->ai_addrlen) == -1) {
                perror("sender: sendto");
            }
        }
        printf("Sender (child) finished sending %d packets.\n", NUM_PACKETS);
        freeaddrinfo(servinfo);
        close(sockfd);
        exit(0);

    } else {
        // --- Parent Process: Receiver ---
        printf("Receiver (parent) process created.\n");
        
        // Set a 1-second timeout on the socket
        struct timeval read_timeout;
        read_timeout.tv_sec = 1;
        read_timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

        int packets_received = 0;
        double min_rtt = (double)LONG_MAX;
        double max_rtt = 0.0;
        double total_rtt = 0.0;
        int received_status[NUM_PACKETS + 1] = {0}; // 0 = not received, 1 = received

        while (packets_received < NUM_PACKETS) {
            char recv_buf[1050];
            int numbytes;

            if ((numbytes = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0, NULL, NULL)) == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("Receiver (parent) timed out. Assuming all packets have been sent.\n");
                    break; // Exit loop if we time out
                }
                perror("receiver: recvfrom");
                break;
            }

            struct timeval tv;
            gettimeofday(&tv, NULL);
            uint64_t recv_time_ms = (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;

            struct PktHeader* recv_header = (struct PktHeader*)recv_buf;
            
            // Extract and convert sequence number
            uint32_t seq_num = ntohl(recv_header->seq_num);
            
            if (seq_num > 0 && seq_num <= NUM_PACKETS && received_status[seq_num] == 0) {
                received_status[seq_num] = 1;
                packets_received++;

                double rtt = (double)(recv_time_ms - recv_header->timestamp);
                total_rtt += rtt;
                if (rtt < min_rtt) min_rtt = rtt;
                if (rtt > max_rtt) max_rtt = rtt;
            }
        }

        // Wait for the child process to terminate
        wait(NULL);
        printf("Receiver (parent) has cleaned up child process.\n\n");

        // --- Print Statistics ---
        printf("------ Statistics Summary ------\n");
        if (packets_received < NUM_PACKETS) {
            int missing_count = NUM_PACKETS - packets_received;
            printf("Missing Echoes: Yes (%d packets lost)\n", missing_count);
        } else {
            printf("Missing Echoes: No\n");
        }

        if (packets_received > 0) {
            printf("Smallest RTT: %.3f ms\n", min_rtt);
            printf("Largest RTT:  %.3f ms\n", max_rtt);
            printf("Average RTT:  %.3f ms\n", total_rtt / packets_received);
        } else {
            printf("No packets were received.\n");
        }
        
        freeaddrinfo(servinfo);
        close(sockfd);
    }
    return 0;
}

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10000

/* Returns speed parameter */
int get_speed(char* param) {
    int p;
    sscanf(param, "speed=%d", &p);
    return p;
}

/* Returns delay parameter */
int get_delay(char* param) {
    int p;
    sscanf(param, "delay=%d", &p);
    return p;
}

/* Returns loss parameter */
double get_loss(char *param) {
    double p;
    sscanf(param, "loss=%lf", &p);
    return p;
}

/* Returns corrupt parameter */
double get_corrupt(char *param) {
    double p;
    sscanf(param, "corrupt=%lf", &p);
    return p;
}

/* Transmit te file */
void send_file(char* filename) {
    msg t;

    struct stat buf;
    if ( stat(filename, &buf) < 0 ) {
        perror("Stat failed");
        return;
    }

    int fd = open(filename, O_RDONLY);

    if (fd < 0) {
        perror("Couldn't open file");
        return;
    }

    /* Type 1: filename + filesize */
    t.type = 1;
    sprintf(t.payload, "%s\n%d\n", filename, (int) buf.st_size);
    t.len = strlen(t.payload) + 1;

    send_message(&t);

    /* Type 2: The actual payload */
    t.type = 2;
    while ( (t.len = read(fd, &t.payload, 1400) ) > 0 ) {
        //printf("Len is %d\n",t.len);
        send_message(&t);

        /* wait for ack, max 10 ms, then cycle - assume no packet loss */
        msg* r = NULL;
        while (!r) {
            r = receive_message_timeout(10);
            //printf("Received: %s\n",r->payload);

            if (!r) {
                /* there was an error on receive! */
                perror("receive error");
            }
        }

        /* wait for ack, blocking */
        //msg* r = receive_message();
        free(r);
    }

    close(fd);
}

int main(int argc, char** argv) {
    init(HOST, PORT);

    if (argc < 2) {
        printf("Usage %s filename\n", argv[0]);
        return -1;
    }

    printf("Speed: %d\n", get_speed(argv[1]));
    printf("Delay: %d\n", get_delay(argv[2]));
    printf("Loss: %lf\n", get_loss(argv[3]));
    printf("Corrupt: %lf\n", get_corrupt(argv[4]));

    send_file(argv[5]);
    return 0;
}

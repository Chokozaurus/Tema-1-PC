#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib.h"

#define HOST "127.0.0.1"
#define PORT 10001

#define FRM_LOAD_SZ 1394

typedef struct {
    char load[FRM_LOAD_SZ];
    unsigned int id;
    unsigned short int crc;
} frame;

int main(int argc, char** argv) {
    msg *r,t;
    init(HOST, PORT);
    char filename[1400], filesize[1400];
    int fs;

    r = receive_message();

    if (!r){
        perror("Receive message");
        return -1;
    }

    /* find the filename */
    int name = 1, crt = 0, i;

    if (r->type != 1){
        printf("Expecting filename and size message\n");
        return -1;
    }

    msg ok;
    ok.type = 1000;
    send_message(&ok);

    for (i = 0; i < r->len; i++){
        if (crt >= 1400){
            printf("Malformed message received! Bailing out");
            return -1;
        }

        if (name){
            if (r->payload[i] == '\n'){
                name = 0;
                filename[crt] = 0;
                crt = 0;
            }
            else 
                filename[crt++] = r->payload[i];
        }
        else {
            if (r->payload[i] == '\n'){
                name = 0;
                filesize[crt] = 0;
                crt = 0;
                break;
            }
            else 
                filesize[crt++] = r->payload[i];
        }
    }
    fs = atoi(filesize);
    char fn[2000];

    sprintf(fn,"recv_%s", filename);
    printf("Receiving file %s of size %d\n", fn, fs);

    /* Open file to write into */
    int fd = open(fn, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("Failed to open file\n");
    }

    while (fs > 0){
        printf("Left to read %d\n", fs);
        r = receive_message();
        if (!r){
            perror("Receive message");
            return -1;
        }

        if (r->type != 2){
            printf("Expecting filename and size message\n");
            return -1;
        }
    
        write(fd, r->payload, r->len);
        fs -= r->len;
        free(r);

        t.type = 3;
        sprintf(t.payload, "ACK");
        t.len = strlen(t.payload) + 1;
        send_message(&t);
    }
    return 0;
}

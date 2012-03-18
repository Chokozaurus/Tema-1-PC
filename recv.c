#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib.h"
#include "crc.h"

#define HOST "127.0.0.1"
#define PORT 10001

#define PAYLOAD_SZ 1400
#define PCK_LOAD_SZ (PAYLOAD_SZ - (sizeof(int) + sizeof(short int)))
#define CRC_LOAD_SZ (PAYLOAD_SZ + (sizeof(int) + sizeof(short int)))

typedef struct _packet {
    char load[PCK_LOAD_SZ];
    unsigned int id;
    word crc;
} packet;

typedef union _charge {
    struct {
        int type;
        int len;
        char payload[1400];
    }msg;
    struct {
        int type;
        int len;
        unsigned int id;
        char load[PCK_LOAD_SZ];
        word crc;
    }pack;
    struct {
        char payload[1406];
        word crc;
    }crc;
}charge;

word *tabel;

/* Compute crc table */
void compcrc( char *data, int len, word *acum ){
    *acum = 0;
    int i;
    for(i = 0; i < len; ++i)
        crctabel(data[i], acum, tabel);
}

int main(int argc, char** argv) {
    msg /* *r*/t;
    init(HOST, PORT);
    char filename[1400], filesize[1400];
    int fs;

    tabel = tabelcrc(CRCCCITT);

    charge *r = NULL;
    r = (charge *)receive_message();

    if (!r){
        perror("Receive message");
        return -1;
    }

    /* find the filename */
    int name = 1, crt = 0, i;

    if (r->msg.type != 1){
        printf("Expecting filename and size message\n");
        return -1;
    }

    //msg ok;
    charge ok;
    fprintf(stderr, "sending ok_\n");
    compcrc( r->crc.payload, CRC_LOAD_SZ, &ok.crc.crc);
    fprintf(stderr, "sending crc_[%u]", ok.crc.crc);
    ok.msg.type = 1000;
    send_message((msg *)&ok);

    for (i = 0; i < r->msg.len; i++){
        if (crt >= 1400){
            printf("Malformed message received! Bailing out");
            return -1;
        }

        if (name){
            if (r->msg.payload[i] == '\n'){
                name = 0;
                filename[crt] = 0;
                crt = 0;
            }
            else 
                filename[crt++] = r->msg.payload[i];
        }
        else {
            if (r->msg.payload[i] == '\n'){
                name = 0;
                filesize[crt] = 0;
                crt = 0;
                break;
            }
            else 
                filesize[crt++] = r->msg.payload[i];
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
        r = (charge *)receive_message();
        if (!r){
            perror("Receive message");
            return -1;
        }

        if (r->msg.type != 2){
            printf("Expecting filename and size message\n");
            return -1;
        }
    
        write(fd, r->msg.payload, r->msg.len);
        fs -= r->msg.len;
        free(r);

        t.type = 3;
        sprintf(t.payload, "ACK");
        t.len = strlen(t.payload) + 1;
        send_message(&t);
    }
    return 0;
}

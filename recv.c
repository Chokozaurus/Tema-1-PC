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
    /* The original struct */
    struct {
        int type;
        int len;
        char payload[PAYLOAD_SZ];   //1400
    }msg;
    /* Struct having id and crc for packages */
    struct {
        int type;
        int len;
        unsigned int id;
        char load[PCK_LOAD_SZ];     //1394
        word crc;
    }pack;
    /* Struct used to easily compute the crc for message */
    struct {
        char payload[CRC_LOAD_SZ];  //1406
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

/* Returns window parameter */
int get_window(char* param) {
    int p;
    sscanf(param, "window=%d", &p);
    return p;
}

int main(int argc, char** argv) {
    msg t;
    init(HOST, PORT);
    char filename[1400], filesize[1400];
    int fs;

    /* Read window parameter */
    int window = get_window( argv[1] );
    int win;
    fprintf(stderr, "recv window : [%d]\n", window);

    tabel = tabelcrc(CRCCCITT);

    /* Receive handshake message
    ****************************
    */
    charge *r = NULL;
    r = (charge *)receive_message();

    if (!r){
        perror("Receive message");
        return -1;
    }

    char fn[2000];

    while (r) {

        /* find the filename */
        int name = 1, crt = 0, i;

        if (r->msg.type != 1){
            printf("Expecting filename and size message\n");
            //return -1;
            continue;
        }

        /* Acknowledge for the filename and size */
        charge ok;
        fprintf(stderr, "sending ok_\n");
        compcrc( r->crc.payload, CRC_LOAD_SZ, &ok.crc.crc);
        fprintf(stderr, "sending crc_[%u]\n", ok.crc.crc);

        if (ok.crc.crc == r->crc.crc) {
            ok.msg.type = 1000;
            send_message((msg *)&ok);

            for (i = 0; i < r->msg.len; i++){
                if (crt >= 1400){
                    printf("Malformed message received! Bailing out");
                    return -1;
                }

                if (name){
                    if (r->pack.load[i] == '\n'){
                        name = 0;
                        filename[crt] = 0;
                        crt = 0;
                    }
                    else
                        filename[crt++] = r->pack.load[i];
                }
                else {
                    if (r->pack.load[i] == '\n'){
                        name = 0;
                        filesize[crt] = 0;
                        crt = 0;
                        break;
                    }
                    else
                        filesize[crt++] = r->pack.load[i];
                }
            }
            fs = atoi(filesize);

            win = r->pack.id;
            fprintf(stderr, "sender window: [%d]\n", win);

            sprintf(fn,"recv_%s", filename);
            printf("Receiving file %s of size %d\n", fn, fs);
            break;
        }
        else {
            fprintf(stderr, "Handshake corrupt, or lost\nRecieving another\n");
            r = (charge *) receive_message();
        }
    }

    /* Send window size to 'sender' */
    charge win_rec;
    memset(&win_rec, 0, sizeof(charge));
    win_rec.msg.type = 2000;
    win_rec.pack.id = (unsigned int) window;    /* The actual info */
    send_message( (msg *)&win_rec );

    if ( window == 0 || window > win )
        window = win;

    fprintf(stderr, "\nRecv window recomputed: [%d] \n", window);

    /* Open file to write into
    **************************
    */
    int fd = open(fn, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("Failed to open file\n");
    }

    /* Process information */
    while (fs > 0){
        printf("Left to read %d\n", fs);
        r = (charge *)receive_message();
        if (!r){
            perror("Receive message");
            return -1;
        }

        if (r->msg.type != 2){
            printf("Expecting payload\n");
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

    close (fd);
    return 0;
}

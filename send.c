#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib.h"
#include "crc.h"

#define HOST "127.0.0.1"
#define PORT 10000

#define PAYLOAD_SZ 1400
#define PCK_LOAD_SZ (PAYLOAD_SZ - (sizeof(int) + sizeof(short int)))
#define CRC_LOAD_SZ (PAYLOAD_SZ + (sizeof(int) + sizeof(short int)))

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

/* Compute crc table */
void compcrc(char *data, int len, word *acum) {
    *acum = 0;
    int i;
    for(i = 0; i < len; ++i)
        crctabel(data[i], acum, tabel);
}

/* Push a charge element into a queue implemented as array */
void push(charge *queue, charge elem, unsigned int front, unsigned int *count, 
        unsigned int max_size) {

    if (*count >= max_size) {
        fprintf(stderr, "Queue size exceded\n");
        exit(1);
    }

    unsigned int new_index;
    new_index = (front + *count) % max_size;
    queue[new_index] = elem;

    (*count)++;
}

/* Pop a charge element from a queue implemented as array */
charge pop(charge *queue, unsigned int *front, unsigned int *count, 
        unsigned int max_size) {

    charge old_elem;

    if (*count <= 0) {
        fprintf(stderr, "Pop on empty queue\n");
        exit(1);
    }

    old_elem = queue[*front];
    memset(&queue[*front], 0, sizeof(charge));

    (*front)++;
    (*front) %= max_size;

    (*count)--;

    return old_elem;
}

/* Transmit the file using Selective Repeat protocol
****************************************************
*/
void transmit(char* filename, int speed, int delay, double loss,
            double corrupt) {

    /* Compute window size */
    int window_sz = (int) ( (double) ( ( (double)(speed * delay) / 8) \
                        * (1 << 20)  ) / (1000 * 1408) + 1 );


/*    fprintf(stderr, "speed [%d], delay [%d]\n", speed, delay);*/
/*    fprintf(stderr, "window_sz: %d\n", window_sz);*/

    /* Attempt to get stats of the file */
    struct stat buf;
    if ( stat(filename, &buf) < 0 ) {
        perror("Stat failed");
        return;
    }

    /* Open the file */
    int file = open(filename, O_RDONLY);

    if (file < 0) {
        perror("Couldn't open file");
        return;
    }

    charge t, *ok = NULL;;
    memset(&t, 0, sizeof(charge));
    int not_sent = 1;

    /* Type 1: filename + filesize */
    t.msg.type = 1;
    sprintf(t.pack.load, "%s\n%d\n", filename, (int) buf.st_size);
    t.msg.len = strlen(t.pack.load) + 1;

    t.pack.id = (unsigned int) window_sz;   /* window size in place of id */

    /* Compute CRC of the handshake message */
    compcrc(t.crc.payload, CRC_LOAD_SZ, &t.crc.crc);
/*    fprintf(stderr, "type: [%d] load [%s] len[%d] crc[%u]\n", t.msg.type, t.pack.load, t.msg.len, t.crc.crc);*/

    /* Make sure the first frame containing filename and its size is recieved */
    while (not_sent) {
        send_message((msg *)&t);
        fprintf(stderr, "Handshake message attempted\n");
        ok = (charge *)receive_message_timeout(delay * 2 + 20);

        if(!ok) {
            fprintf(stderr, "Handshake message receive failure\n");
            continue;
        }

/*        fprintf(stderr, "crc_recieved: [%u]\n", ok->crc.crc);*/
        if (ok->msg.type == 1000 && ok->crc.crc == t.crc.crc)
            not_sent = 0;
/*        fprintf(stderr, "ok-type: [%d]\n", ok->msg.type);*/
        free(ok);
    }

    /* Get window size from reciever */
    charge *win_rec = NULL;
    win_rec = (charge *)receive_message();
    if (!win_rec) {
        fprintf(stderr, "Window size not recieved\n");
    }
    
    if (win_rec->msg.type != 2000)
        fprintf(stderr, "Error, window size from reciever expected\n");
        
    const int win_recv = win_rec->pack.id;
/*    fprintf(stderr, "win_recv: [%d]\n", win_recv);*/
    
    free(win_rec);

    if (win_recv != 0 && window_sz > win_recv)
        window_sz = win_recv;

    if (window_sz < 10)
        window_sz = 10;

/*    fprintf(stderr, "win_sender recomputed: [%d]\n", window_sz);*/


    /* Transmit the content of the file 
    ************************************
    */
    unsigned int seq = 0;
    unsigned int front = 0, count = 0;
    charge *buff = (charge *) calloc(window_sz, sizeof(charge));
    charge tr, *rr;
    memset(&tr, 0, sizeof(charge));
    int flen = (int) buf.st_size;

    while (flen) {
        if ( (tr.msg.len = read(file, &tr.pack.load, PCK_LOAD_SZ) ) > 0 ) {
            tr.msg.type = 2;    /* Data */
            tr.pack.id = seq;
            compcrc(tr.crc.payload, CRC_LOAD_SZ, &tr.crc.crc);

            send_message((msg *) &tr);
            push(buff, tr, front, &count, window_sz);
            //fprintf(stderr, "push_nr: [%u]\tcount: [%u]\n", seq, count);
            
            seq++;

            if ( count < window_sz && tr.msg.len == PCK_LOAD_SZ ) {
                memset(&tr, 0, sizeof(charge));
                continue;   /* Fulfill buffer */
            }
            
        }
        
        read:
        /* Read ACK or NAK */
        rr = NULL;
        rr = (charge *)receive_message_timeout(delay * 2 + 20);
        charge nkd, ackd;
        
        //fprintf(stderr, "rr->pack.type: [%u]\n", rr->pack.type);
        if (rr != NULL)
            switch (rr->pack.type) {
            /* Case for NAK */
            case 4:
                //fprintf(stderr, "NAK\n");
                t_out:
/*                fprintf(stderr, "NAK\tfront: [%u]\tcount: [%u]\n", front, count);*/
                nkd = pop(buff, &front, &count, window_sz);
                send_message((msg *) &nkd);
                push(buff, nkd, front, &count, window_sz);
                
                free(rr);
                goto read;
                break;
            /* Case for ACK */
            case 3:
                //fprintf(stderr, "ACK\n");
                ackd = pop(buff, &front, &count, window_sz);
/*                fprintf(stderr, "ackd-id: [%u]\t rr-id-cmp: [%u]\n", ackd.pack.id, rr->pack.id);*/
                while ( ackd.pack.id != rr->pack.id ) {
/*                    fprintf(stderr, "rr-id: [%u]\tfront: [%u]\tcount: [%u]\n", ackd.pack.id, front, count);*/
                    send_message((msg *) &ackd);
                    push(buff, ackd, front, &count, window_sz);
                    
                    ackd = pop(buff, &front, &count, window_sz);
                    //exit(1);
                }
                flen -= ackd.pack.len;
                free(rr);
                //ackd = pop(buff, &front, &count, window_sz);
                break;
            default:
                break;
            }
            else
                goto t_out;
    }

/*    fprintf(stderr, "Before close_file\n");*/
    close(file);
/*    fprintf(stderr, "After close_file && before free\n");*/
    free(buff);
/*    fprintf(stderr, "After free\n");*/
}

int main(int argc, char** argv) {
    init(HOST, PORT);

    if (argc < 2) {
        printf("Usage %s filename\n", argv[0]);
        return -1;
    }

    tabel = tabelcrc(CRCCCITT);

/*    printf("Speed: %d\n", get_speed(argv[1]));*/
/*    printf("Delay: %d\n", get_delay(argv[2]));*/
/*    printf("Loss: %lf\n", get_loss(argv[3]));*/
/*    printf("Corrupt: %lf\n", get_corrupt(argv[4]));*/

    transmit(argv[5], get_speed(argv[1]), get_delay(argv[2]), get_loss(argv[3]),
            get_corrupt(argv[4]) );

/*    fprintf(stderr, "Out of transmit\n");*/
    
    free(tabel);
    return 0;
}

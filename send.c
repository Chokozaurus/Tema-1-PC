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

    if (*count >= 10) {
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


    fprintf(stderr, "speed [%d], delay [%d]\n", speed, delay);
    fprintf(stderr, "window_sz: %d\n", window_sz);

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
    fprintf(stderr, "type: [%d] load [%s] len[%d] crc[%u]\n", t.msg.type, t.pack.load, t.msg.len, t.crc.crc);

    /* Make sure the first frame containing filename and its size is recieved */
    while (not_sent) {
        send_message((msg *)&t);
        fprintf(stderr, "Handshake message attempted\n");
        ok = (charge *)receive_message();//_timeout(delay + 20);

        if(!ok) {
            fprintf(stderr, "Handshake message receive failure\n");
            continue;
        }

        fprintf(stderr, "crc_recieved: [%u]\n", ok->crc.crc);
        if (ok->msg.type == 1000 && ok->crc.crc == t.crc.crc)
            not_sent = 0;
        fprintf(stderr, "ok-type: [%d]\n", ok->msg.type);
    }

    charge *win_rec = NULL;
    win_rec = (charge *)receive_message();
    if (!win_rec) {
        fprintf(stderr, "Not recieved\n");
    }
    if (win_rec->msg.type != 2000)
        fprintf(stderr, "Error, window size from reciever expected\n");
    const int win_recv = win_rec->pack.id;
    fprintf(stderr, "win_recv: [%d]\n", win_recv);

    if (win_recv != 0 && window_sz > win_recv)
        window_sz = win_recv;

    if (window_sz < 10)
        window_sz = 10;

    fprintf(stderr, "win_sender recomputed: [%d]\n", window_sz);


    /* Transmit the content of the file 
    ************************************
    */
    unsigned int seq = 0;
    unsigned int front = 0, count = 0;
    charge *buff = (charge *) calloc(window_sz, sizeof(charge));
    charge tr, *rr;
    memset(&tr, 0, sizeof(charge));

    while ( (tr.msg.len = read(file, &tr.pack.load, PCK_LOAD_SZ) ) > 0 ) {
        tr.msg.type = 2;    /* Data */
        tr.pack.id = seq;
        compcrc(tr.crc.payload, CRC_LOAD_SZ, &tr.crc.crc);

        /* Queue tests
        ***************************************************
        */

/*        fprintf(stderr, "tr-id: [%u]\ttr-type: [%d]\ttr-len: [%d]\t tr-crc: [%d]\n", tr.pack.id,*/
/*                tr.msg.type, tr.msg.len, tr.pack.crc);*/

        //memcpy(&buff[seq % window_sz], &tr, sizeof(tr) );
        
/*        push(buff, tr, front, &count, window_sz);*/
/*        fprintf(stderr, "\nTEST_QUEUE:\nfront: [%u]\tcount: [%u]\tbuff[front].pack.crc: [%u]\n",*/
/*            front, count, buff[front].pack.crc);*/
/*        charge test = pop(buff, &front, &count, window_sz);*/
/*        fprintf(stderr, "\nTEST_QUEUE_POP:\nfront: [%u]\tcount: [%u]\ttest.pack.crc: [%u]\n",*/
/*            front, count, test.pack.crc);*/
/*            */
        
/*        fprintf(stderr, "buf-id: [%u]\tbuf-type: [%d]\tbuf-len: [%d]\t buf-crc: [%d]\n", buff[seq%window_sz].pack.id,*/
/*                buff[seq%window_sz].msg.type, buff[seq%window_sz].msg.len, buff[seq%window_sz].pack.crc);*/

        send_message((msg *) &tr);
        push(buff, tr, front, &count, window_sz);
        //fprintf(stderr, "push_nr: [%u]\tcount: [%u]\n", seq, count);
        
        seq++;

        //memset(&tr, 0, sizeof(charge));
        

        if ( count < window_sz && tr.msg.len == PCK_LOAD_SZ ) {
            memset(&tr, 0, sizeof(charge));
            continue;   /* Fulfill buffer */
        }

        /* Read ACK or NAK */
        rr = (charge *)receive_message_timeout(delay * 2 + 20);
        charge nkd, ackd;
        
        //fprintf(stderr, "rr->pack.type: [%u]\n", rr->pack.type);
        
        switch (rr->pack.type) {
            /* Case for NAK */
            case 4:
                fprintf(stderr, "NAK\n");
                nkd = pop(buff, &front, &count, window_sz);
                send_message((msg *) &nkd);
                push(buff, nkd, front, &count, window_sz);
                break;
            /* Case for ACK */
            case 3:
                //fprintf(stderr, "ACK\n");
                ackd = pop(buff, &front, &count, window_sz);
                //fprintf(stderr, "ackd-id: [%u]\t rr-id-cmp: [%u]\n", ackd.pack.id, rr->pack.id);
                while ( ackd.pack.id != rr->pack.id ) {
                    //fprintf(stderr, "rr-id: [%u]\tfront: [%u]\tcount: [%u]\n", ackd.pack.id, front, count);
                    send_message((msg *) &ackd);
                    push(buff, ackd, front, &count, window_sz);
                    
                    ackd = pop(buff, &front, &count, window_sz);
                    //exit(1);
                }
                //ackd = pop(buff, &front, &count, window_sz);
                break;
            default:
                break;
            }
        
/*        while ( count < window_sz &&*/
/*            (tr.msg.len = read(file, &tr.pack.load, PCK_LOAD_SZ) ) > 0 ) {*/
/*            */
//            tr.msg.type = 2;    /* Data */
/*            tr.pack.id = seq;*/
/*            compcrc(tr.crc.payload, CRC_LOAD_SZ, &tr.crc.crc);*/
/*        }*/
    }
/*    send_message( (msg *) &buff[0] );*/
    //rr = receive_message_timeout(delay+20);

    fprintf(stderr, "Before close_file\n");
    close(file);
    fprintf(stderr, "After close_file && before free\n");
    free(buff);
    fprintf(stderr, "After free\n");
}

/* Transmit te file start-stop */
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

    tabel = tabelcrc(CRCCCITT);

    printf("Speed: %d\n", get_speed(argv[1]));
    printf("Delay: %d\n", get_delay(argv[2]));
    printf("Loss: %lf\n", get_loss(argv[3]));
    printf("Corrupt: %lf\n", get_corrupt(argv[4]));

    transmit(argv[5], get_speed(argv[1]), get_delay(argv[2]), get_loss(argv[3]),
            get_corrupt(argv[4]) );

    fprintf(stderr, "Out of transmit\n");
    //send_file(argv[5]);
    return 0;
}

// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define BAUDRATE B38400

#define FLAG 0x7E
#define A 0x03
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
#define C_RR(r) 0x05 // | ?
#define C_REJ(r) 0x01 // | ?

typedef enum {
    START_STATE,
    FLAG_RCV_STATE,
    A_RCV_STATE,
    C_RCV_STATE,
    BCC_OK_STATE,
    STOP_STATE
} State;

int alarmEnabled = FALSE;
int alarmCount = 0;
int nRetransmissions;
int timeout;

void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    // TODO

    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1) {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == 1) {
        perror("tcsetattr");
        exit(-1);
    }

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    State state = START_STATE;
    unsigned char byte_read;
    unsigned char a_check;
    unsigned char c_check;

    if (connectionParameters.role == LlTx) {
        //(void) signal(SIGALRM, alarmHandler);

        unsigned char buffer[5] = {FLAG, A, C_SET, A ^ C_SET, FLAG};
        write(fd, buffer, sizeof(buffer));

        while (state != STOP_STATE) {
            printf("Tx: state %d\n", state);
            if (read(fd, &byte_read, sizeof(byte_read)) == sizeof(byte_read)) {
                printf("Tx: byte read\n");
                switch (state) {
                    case START_STATE:
                        printf("Tx: START_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else state = START_STATE;
                        break;
                    case FLAG_RCV_STATE:
                        printf("Tx: FLAG_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == A) {
                            a_check = byte_read;
                            state = A_RCV_STATE;
                        }
                        else state = START_STATE;
                        break;
                    case A_RCV_STATE:
                        printf("Tx: A_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == C_UA) {
                            c_check = byte_read;
                            state = C_RCV_STATE;
                        }
                        else state = START_STATE;
                        break;
                    case C_RCV_STATE:
                        printf("Tx: C_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == (a_check ^ c_check)) state = BCC_OK_STATE;
                        else state = START_STATE;
                        break;
                    case BCC_OK_STATE:
                        printf("Tx: BCC_OK_STATE\n");
                        if (byte_read == FLAG) state = STOP_STATE;
                        else state = START_STATE;
                        break;
                    default:
                        break;
                }
            }
        }
    } else if (connectionParameters.role == LlRx) {
        while (state != STOP_STATE) {
            printf("Rx: state %d\n", state);
            if (read(fd, &byte_read, sizeof(byte_read)) == sizeof(byte_read)) {
                printf("Rx: byte read\n");
                switch (state) {
                    case START_STATE:
                        printf("Rx: START_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else state = START_STATE;
                        break;
                    case FLAG_RCV_STATE:
                    printf("Rx: FLAG_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == A) {
                            a_check = byte_read;
                            state = A_RCV_STATE;
                        }
                        else state = START_STATE;
                        break;
                    case A_RCV_STATE:
                    printf("Rx: A_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == C_SET) {
                            c_check = byte_read;
                            state = C_RCV_STATE;
                        }
                        else state = START_STATE;
                        break;
                    case C_RCV_STATE:
                    printf("Rx: C_RCV_STATE\n");
                        if (byte_read == FLAG) state = FLAG_RCV_STATE;
                        else if (byte_read == (a_check ^ c_check)) state = BCC_OK_STATE;
                        else state = START_STATE;
                        break;
                    case BCC_OK_STATE:
                    printf("Rx: BCC_OK_STATE\n");
                        if (byte_read == FLAG) state = STOP_STATE;
                        else state = START_STATE;
                        break;
                    default:
                        break;
                }
            }
        }
        unsigned char buffer[5] = {FLAG, A, C_UA, A ^ C_UA, FLAG};
        write(fd, buffer, sizeof(buffer));
    } else {
        perror("connectionParameters.role");
        exit(-1);
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}

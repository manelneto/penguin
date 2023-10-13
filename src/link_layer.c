// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1  // POSIX compliant source

#define BAUDRATE B38400

#define FLAG 0x7E
#define A 0x03
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
#define C_RR(r) ((r << 7) | 0x05)
#define C_REJ(r) ((r << 7) | 0x01)

#define N_0 0x00
#define N_1 0x40
#define N(s) s << 7

#define ESC 0x7D
#define FLAG_ESCAPED 0x5E
#define ESC_ESCAPED 0x5D

typedef enum {
    START_STATE,
    FLAG_RCV_STATE,
    A_RCV_STATE,
    C_RCV_STATE,
    BCC_OK_STATE,
    STOP_STATE
} State;

int fd;
int alarmEnabled = FALSE;
int alarmCount = 0;
int nRetransmissions;
int timeout;
LinkLayerRole role;

void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;
    printf("ALARM\n");
}

void processByte(int fd, unsigned char actual_c, unsigned char *a_check, unsigned char *c_check, State *state) {
    unsigned char byte_read;

    // printf("State: %d\n", *state);
    if (read(fd, &byte_read, sizeof(byte_read)) == sizeof(byte_read)) {
        printf("Byte read: 0x%0x\n", byte_read);
        switch (*state) {
            case START_STATE:
                printf("START_STATE\n");
                if (byte_read == FLAG)
                    *state = FLAG_RCV_STATE;
                else
                    *state = START_STATE;
                break;
            case FLAG_RCV_STATE:
                printf("FLAG_RCV_STATE\n");
                if (byte_read == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byte_read == A) {
                    *a_check = byte_read;
                    *state = A_RCV_STATE;
                } else
                    *state = START_STATE;
                break;
            case A_RCV_STATE:
                printf("A_RCV_STATE\n");
                if (byte_read == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byte_read == actual_c) {
                    *c_check = byte_read;
                    *state = C_RCV_STATE;
                } else
                    *state = START_STATE;
                break;
            case C_RCV_STATE:
                printf("C_RCV_STATE\n");
                if (byte_read == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byte_read == (*a_check ^ *c_check))
                    *state = BCC_OK_STATE;
                else
                    *state = START_STATE;
                break;
            case BCC_OK_STATE:
                printf("BCC_OK_STATE\n");
                if (byte_read == FLAG)
                    *state = STOP_STATE;
                else
                    *state = START_STATE;
                break;
            default:
                break;
        }
    }
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    // TODO

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
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
    int tries = nRetransmissions;

    timeout = connectionParameters.timeout;
    role = connectionParameters.role;
    
    State state = START_STATE;
    unsigned char a_check;
    unsigned char c_check;

    if (connectionParameters.role == LlTx) {
        (void)signal(SIGALRM, alarmHandler);
        unsigned char buffer[5] = {FLAG, A, C_SET, A ^ C_SET, FLAG};
        do {
            write(fd, buffer, sizeof(buffer));
            alarm(timeout);
            alarmEnabled = TRUE;
            while (alarmEnabled == TRUE && state != STOP_STATE)
                processByte(fd, C_UA, &a_check, &c_check, &state);
            if (state == STOP_STATE) {
                alarm(0);
                alarmEnabled = FALSE;
            }
            printf("nRetransmissions: %d\n", tries);
            tries--;
            alarmEnabled = TRUE;
        } while (tries > 0 && state != STOP_STATE);
    } else if (connectionParameters.role == LlRx) {
        while (state != STOP_STATE) {
            processByte(fd, C_SET, &a_check, &c_check, &state);
        }
        unsigned char buffer[5] = {FLAG, A, C_UA, A ^ C_UA, FLAG};
        write(fd, buffer, sizeof(buffer));
    } else {
        perror("connectionParameters.role");
        exit(-1);
    }

    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    unsigned char bcc2 = buf[0];
    printf("buf[i: %d]: 0x%x\n", 0, buf[0]);
    printf("bufsize: %d\n", bufSize);
    for (int i = 1; i < bufSize; i++) {
        printf("buf[i: %d]: 0x%x\n", i, buf[i]);
        bcc2 ^= buf[i];
        printf("BCC2: 0x%x\n", bcc2);
    }

    unsigned char *dataBcc2 = malloc(2 * bufSize + 2);
    unsigned index = 0;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG) {
            dataBcc2[index++] = ESC;
            dataBcc2[index++] = FLAG_ESCAPED;
        } else if (buf[i] == ESC) {
            dataBcc2[index++] = ESC;
            dataBcc2[index++] = ESC_ESCAPED;
        } else {
            dataBcc2[index++] = buf[i];
        }
    }

    if (bcc2 == FLAG) {
        dataBcc2[index++] = ESC;
        dataBcc2[index++] = FLAG_ESCAPED;
    } else if (buf[index] == ESC) {
        dataBcc2[index++] = ESC;
        dataBcc2[index++] = ESC_ESCAPED;
    } else {
        dataBcc2[index++] = bcc2;
    }

    static unsigned char tramaI = 0;
    unsigned char n;
    if (tramaI == 0) {
        n = N_0;
    } else if (tramaI == 1) {
        n = N_1;
    }

    unsigned char bcc1 = A ^ n;

    unsigned char *frame = malloc(index + 5);  // F A C BCC1 F;
    frame[0] = FLAG;
    frame[1] = A;
    frame[2] = n;
    frame[3] = bcc1;
    for (int i = 0; i < index; i++) {
        frame[i] = dataBcc2[i];
    }
    frame[index + 4] = FLAG;

    int tries = nRetransmissions;
    int rejectedCheck = FALSE;
    int accepetedCheck = FALSE;
    State state = START_STATE;
    unsigned char byte_read;
    unsigned char a_check;
    unsigned char c_check;
    do {
        write(fd, frame, sizeof(frame));  // <-----
        alarm(timeout);
        alarmEnabled = TRUE;
        while (alarmEnabled == TRUE && rejectedCheck == FALSE && accepetedCheck == FALSE) {
            // state machine
            while (state != STOP_STATE && alarmEnabled == TRUE) {
                if (read(fd, &byte_read, sizeof(byte_read)) == sizeof(byte_read)) {
                    printf("Byte read: 0x%0x\n", byte_read);
                    switch (state) {
                        case START_STATE:
                            printf("START_STATE\n");
                            if (byte_read == FLAG) state = FLAG_RCV_STATE;
                            else state = START_STATE;
                            break;
                        case FLAG_RCV_STATE:
                            printf("FLAG_RCV_STATE\n");
                            if (byte_read == FLAG) state = FLAG_RCV_STATE;
                            else if (byte_read == A) {
                                a_check = byte_read;
                                state = A_RCV_STATE;
                            } else state = START_STATE;
                            break;
                        case A_RCV_STATE:
                            printf("A_RCV_STATE\n");
                            if (byte_read == FLAG) state = FLAG_RCV_STATE;
                            else if (byte_read == C_RR(0) || byte_read == C_RR(1) || byte_read == C_REJ(0) || byte_read == C_REJ(1)) {
                                c_check = byte_read;
                                state = C_RCV_STATE;
                            } else state = START_STATE;
                            break;
                        case C_RCV_STATE:
                            printf("C_RCV_STATE\n");
                            if (byte_read == FLAG) state = FLAG_RCV_STATE;
                            else if (byte_read == (a_check ^ c_check)) state = BCC_OK_STATE;
                            else state = START_STATE;
                            break;
                        case BCC_OK_STATE:
                            printf("BCC_OK_STATE\n");
                            if (byte_read == FLAG) state = STOP_STATE;
                            else state = START_STATE;
                            break;
                        default:
                            break;
                    }
                }
            }
            // interpretação da resposta
            if (c_check == C_RR(0) || c_check == C_RR(1)) {
                accepetedCheck = TRUE;
                tramaI = (tramaI + 1) % 2;
            }
            if (c_check == C_REJ(0) || c_check == C_REJ(1)) {
                rejectedCheck = TRUE;
            } 
        }
        if (accepetedCheck == TRUE) {
            break;
        }
        printf("tries: %d\n", tries);
        tries--;
        alarmEnabled = TRUE;
    } while (tries > 0 && state != STOP_STATE);
    free(dataBcc2);
    free(frame);
    return rejectedCheck;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    
    State state = START_STATE;
    unsigned char a_check;
    unsigned char c_check;

    int tries = nRetransmissions;

    if (role == LlTx) {
        unsigned char bufferDisc[5] = {FLAG, A, C_DISC, A ^ C_DISC, FLAG};
        do {
            write(fd, bufferDisc, sizeof(bufferDisc));
            alarm(timeout);
            alarmEnabled = TRUE;
            while (alarmEnabled == TRUE && state != STOP_STATE)
                processByte(fd, C_DISC, &a_check, &c_check, &state);
            if (state == STOP_STATE) {
                alarm(0);
                alarmEnabled = FALSE;
            }
            printf("nRetransmissions: %d\n", nRetransmissions);
            tries--;
            alarmEnabled = TRUE;
        } while (tries > 0 && state != STOP_STATE);

        unsigned char bufferUa[5] = {FLAG, A, C_UA, A ^ C_UA, FLAG};
        write(fd, bufferUa, sizeof(bufferUa));

    } else if (role == LlRx) {
        while (state != STOP_STATE) {
            processByte(fd, C_DISC, &a_check, &c_check, &state);
        }
        unsigned char buffer[5] = {FLAG, A, C_DISC, A ^ C_DISC, FLAG};
        write(fd, buffer, sizeof(buffer));
        /*state = START_STATE;
        while (state != STOP_STATE) {
            processByte(fd, C_UA, &a_check, &c_check, &state);
        }*/
    } else {
        perror("connectionParameters.role");
        exit(-1);
    }

    close(fd);
    return 0;
}

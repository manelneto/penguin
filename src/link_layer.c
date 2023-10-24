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
#define A_CLOSE 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_RR(r) (((r) << 7) | 0x05)
#define C_REJ(r) (((r) << 7) | 0x01)
#define C_DISC 0x0B

#define N(s) ((s) << 6)

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

// Imprime "Link Layer" seguido do título e do conteúdo
void printLL(char *title, unsigned char *content, int contentSize) {
    // DEBUG
    printf("\nLink Layer\n");
    for (int i = 0; title[i] != '\0'; i++) printf("%c", title[i]);
    printf("\n");
    for (int i = 0; i < contentSize; i++) printf("0x%x ", content[i]);
    printf("\n");
}

// Lida com uma interrupção do alarme: desativa-o, incrementa um contador e imprime "ALARM"
void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;
    printf("\nALARM\n");
}

/**
 * Máquina de estados que processa cada byte lido da porta série
 * @param a valor esperado no campo A
 * @param c1 um dos possíveis valores esperados no campo C
 * @param c2 outro dos possíveis valores esperados no campo C
 * @param aCheck valor lido do campo A
 * @param cCheck valor lido do campo C
 * @param state estado atual
 * 
 * @details
 * A existência dos parâmetros c1 e c2 permite aproveitar a mesma máquina de estados para llopen, llwrite, llread e llclose
 * Em llopen, só existe um valor esperado para o campo C (C_SET ou C_UA), pelo que c1 = c2
 * Em llread, existem dois valores esperados para o campo C (C_RR e C_REJ), pelo que c1 != c2
 * Em llwrite, existem dois valores esperados para o campo C (N(0) e N(1)), pelo que c1 != c2
 * Em llclose, só existe um valor esperado para o campo C (C_DISC), pelo que c1 = c2
*/
void processByte(unsigned char a, unsigned char c1, unsigned char c2, unsigned char *aCheck, unsigned char *cCheck, State *state) {
    unsigned char byteRead;

    if (read(fd, &byteRead, sizeof(byteRead)) == sizeof(byteRead)) {
        printLL("Byte", &byteRead, sizeof(byteRead));
        switch (*state) {
            case START_STATE:
                if (byteRead == FLAG)
                    *state = FLAG_RCV_STATE;
                else
                    *state = START_STATE;
                break;
            case FLAG_RCV_STATE:
                if (byteRead == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byteRead == a) {
                    *aCheck = byteRead;
                    *state = A_RCV_STATE;
                } else
                    *state = START_STATE;
                break;
            case A_RCV_STATE:
                if (byteRead == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byteRead == c1 || byteRead == c2) {
                    *cCheck = byteRead;
                    *state = C_RCV_STATE;
                } else
                    *state = START_STATE;
                break;
            case C_RCV_STATE:
                if (byteRead == FLAG)
                    *state = FLAG_RCV_STATE;
                else if (byteRead == (*aCheck ^ *cCheck))
                    *state = BCC_OK_STATE;
                else
                    *state = START_STATE;
                break;
            case BCC_OK_STATE:
                if (byteRead == FLAG)
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
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        printf("Erro a abrir a porta série %s\n", connectionParameters.serialPort);
        return -1;
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1) {
        printf("Erro a usar tcgetattr\n");
        return -1;
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        printf("Erro a usar tcsetattr\n");
        return -1;
    }

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;

    State state = START_STATE;
    unsigned char aCheck;
    unsigned char cCheck;

    if (connectionParameters.role == LlTx) {
        (void)signal(SIGALRM, alarmHandler);
        int tries = nRetransmissions;
        unsigned char set[5] = {FLAG, A, C_SET, A ^ C_SET, FLAG};

        do {
            printLL("LLOPEN - enviado SET", set, sizeof(set));  // DEBUG
            write(fd, set, sizeof(set));
            alarm(timeout);
            alarmEnabled = TRUE;
            while (alarmEnabled == TRUE && state != STOP_STATE) {
                // Enquanto o alarme não tiver disparado e estado não for o final, processa os bytes da porta série (um de cada vez)
                processByte(A, C_UA, C_UA, &aCheck, &cCheck, &state);  // Espera um UA
            }
            if (state == STOP_STATE) {
                // O estado final foi alcançado, pelo que o alarme pode ser desativado
                alarm(0);
                alarmEnabled = FALSE;
            } else {
                // O alarme tocou, pelo que ocorreu timeout e deve haver retransmissão (se ainda não tiver sido excedido o número máximo de tentativas)
                tries--;
            }
        } while (tries >= 0 && state != STOP_STATE);

        if (state != STOP_STATE) {
            // Foi excedido o número máximo de tentativas de retransmissão
            printf("LLOPEN - UA não foi recebido\n");
            return -1;
        }
    } else if (connectionParameters.role == LlRx) {
        while (state != STOP_STATE) {
            // Processa os bytes da porta série (um de cada vez)
            processByte(A, C_SET, C_SET, &aCheck, &cCheck, &state);  // Espera um SET
        }
        unsigned char ua[5] = {FLAG, A, C_UA, A ^ C_UA, FLAG};
        write(fd, ua, sizeof(ua));  // Quando receber o SET, responde com UA
    } else {
        printf("Erro em connectionParameters.role\n");
        return -1;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    unsigned char bcc2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }

    unsigned char *dataBcc2 = (unsigned char *)malloc(2 * bufSize + 2);
    int index = 0;
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
    } else if (bcc2 == ESC) {
        dataBcc2[index++] = ESC;
        dataBcc2[index++] = ESC_ESCAPED;
    } else {
        dataBcc2[index++] = bcc2;
    }

    static unsigned char tramaI = 0;
    unsigned char n = N(tramaI);
    unsigned char bcc1 = A ^ n;

    unsigned char next = (tramaI + 1) % 2;

    unsigned char *frame = malloc(index + 5);  // F A C BCC1 F;
    frame[0] = FLAG;
    frame[1] = A;
    frame[2] = n;
    frame[3] = bcc1;
    for (int i = 4; i < (index + 4); i++) {
        frame[i] = dataBcc2[i - 4];
    }
    frame[index + 4] = FLAG;

    int size = index + 5;  // F A C BCC1 F

    State state = START_STATE;
    unsigned char aCheck;
    unsigned char cCheck;
    unsigned char accepetedCheck;
    unsigned char rejectedCheck;

    int tries = nRetransmissions;

    do {
        printLL("LL WRITE - Tx enviou: ", frame, size);
        write(fd, frame, size);
        alarm(timeout);
        alarmEnabled = TRUE;
        accepetedCheck = FALSE;
        rejectedCheck = FALSE;
        while (alarmEnabled == TRUE && rejectedCheck == FALSE && accepetedCheck == FALSE) {
            state = START_STATE;
            while (state != STOP_STATE && alarmEnabled == TRUE) {
                processByte(A, C_RR(next), C_REJ(tramaI), &aCheck, &cCheck, &state);
            }

            if (state == STOP_STATE) {
                alarm(0);
                alarmEnabled = FALSE;
            } else {
                // timeout
                tries--;
                continue;
            }

            // Interpretação da Resposta
            if (cCheck == C_RR(next)) {
                accepetedCheck = TRUE;
                tramaI = next;
            } else if (cCheck == C_REJ(tramaI)) {
                rejectedCheck = TRUE;
            }
        }
    } while (tries >= 0 && accepetedCheck == FALSE);

    if (state != STOP_STATE) {
        printf("LLWRITE: Tx did not receive answer from Rx\n");
        return -1;
    }

    free(dataBcc2);
    free(frame);
    return size;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    static unsigned char tramaI = 0;

    State state = START_STATE;
    unsigned char byteRead;
    unsigned char aCheck;
    unsigned char cCheck;
    unsigned char escFound = FALSE;

    int index = 0;
    int size;

    while (state != BCC_OK_STATE) {
        processByte(A, N(0), N(1), &aCheck, &cCheck, &state);
    }

    if (cCheck != N(tramaI)) {
        // recebeu trama que não estava à espera
        while (byteRead != FLAG) {
            // ignora - limpa a porta série
            read(fd, &byteRead, sizeof(byteRead));
        }
        // responde a indicar qual é a trama que está pronto para receber
        unsigned char n = C_RR(tramaI);
        unsigned char rr[5] = {FLAG, A, n, A ^ n, FLAG};
        write(fd, rr, sizeof(rr));
        return -1;
    }

    while (state != STOP_STATE) {
        if (state == BCC_OK_STATE && read(fd, &byteRead, sizeof(byteRead)) == sizeof(byteRead)) {
            if (escFound) {
                if (byteRead == FLAG_ESCAPED)
                    packet[index++] = FLAG;
                else if (byteRead == ESC_ESCAPED)
                    packet[index++] = ESC;

                escFound = FALSE;
            } else if (byteRead == ESC) {
                escFound = TRUE;
            } else if (byteRead == FLAG) {
                size = index + 5;  // F A C BCC1 F
                unsigned char bcc2 = packet[index - 1];
                index--;
                packet[index] = '\0';
                printLL("Recetor recebeu: ", packet, index);
                unsigned char bcc2Acc = packet[0];
                for (int i = 1; i < index; i++) {
                    bcc2Acc ^= packet[i];
                }
                if (bcc2 == bcc2Acc) {
                    // success
                    tramaI = (tramaI + 1) % 2;
                    unsigned char n = C_RR(tramaI);
                    unsigned char rr[5] = {FLAG, A, n, A ^ n, FLAG};
                    write(fd, rr, sizeof(rr));
                    state = STOP_STATE;
                } else {
                    // error
                    unsigned char n = C_REJ(tramaI);
                    unsigned char rej[5] = {FLAG, A, n, A ^ n, FLAG};
                    write(fd, rej, sizeof(rej));
                    state = STOP_STATE;
                    return -1;
                }
            } else {
                packet[index++] = byteRead;
            }
        }
    }
    return size;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    // TODO - showStatistics
    State state = START_STATE;
    unsigned char aCheck;
    unsigned char cCheck;

    int tries = nRetransmissions;

    if (role == LlTx) {
        unsigned char disc[5] = {FLAG, A, C_DISC, A ^ C_DISC, FLAG};
        do {
            printLL("LLCLOSE - enviado DISC", disc, sizeof(disc));  // DEBUG
            write(fd, disc, sizeof(disc));
            alarm(timeout);
            alarmEnabled = TRUE;
            while (alarmEnabled == TRUE && state != STOP_STATE) {
                // Enquanto o alarme não tiver disparado e estado não for o final, processa os bytes da porta série (um de cada vez)
                processByte(A_CLOSE, C_DISC, C_DISC, &aCheck, &cCheck, &state);  // Espera um DISC
            }
            if (state == STOP_STATE) {
                // O estado final foi alcançado, pelo que o alarme pode ser desativado
                alarm(0);
                alarmEnabled = FALSE;
            } else {
                // O alarme tocou, pelo que ocorreu timeout e deve haver retransmissão (se ainda não tiver sido excedido o número máximo de tentativas)
                tries--;
            }
        } while (tries >= 0 && state != STOP_STATE);

        if (state != STOP_STATE) {
            // Foi excedido o número máximo de tentativas de retransmissão
            printf("LLCLOSE - DISC não foi recebido\n");
            return -1;
        }

        unsigned char ua[5] = {FLAG, A_CLOSE, C_UA, A_CLOSE ^ C_UA, FLAG};
        printLL("LLCLOSE - enviado UA", ua, sizeof(ua));  // DEBUG
        write(fd, ua, sizeof(ua));
    } else if (role == LlRx) {
        while (state != STOP_STATE) {
            // Processa os bytes da porta série (um de cada vez)
            processByte(A, C_DISC, C_DISC, &aCheck, &cCheck, &state);  // Espera um DISC
        }
        unsigned char disc[5] = {FLAG, A_CLOSE, C_DISC, A_CLOSE ^ C_DISC, FLAG};
        printLL("LLCLOSE - enviado DISC", disc, sizeof(disc));  // DEBUG
        write(fd, disc, sizeof(disc));
    } else {
        printf("Erro em connectionParameters.role\n");
        return -1;
    }

    close(fd);
    return 1;
}

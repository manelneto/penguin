// Application layer protocol implementation

#include "application_layer.h"

#include <stdlib.h>
#include <string.h>

#include "link_layer.h"

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    // TODO
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    if (strcmp(role, "tx") == 0)  // role == "tx"
        connectionParameters.role = LlTx;
    else if (strcmp(role, "rx") == 0)  // role == "rx"
        connectionParameters.role = LlRx;
    else
        return;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    llopen(connectionParameters);
    if (connectionParameters.role == LlTx) {
        unsigned char buf1[5] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
        llwrite(buf1, 5);
        //unsigned char buf2[5] = {0x2A, 0x3B, 0x4C, 0x5D, 0x6E};
        //llwrite(buf2, 5);
    } else if (connectionParameters.role == LlRx) {
        unsigned char *buf1 = (unsigned char *)malloc(5);
        while (llread(buf1));
        //unsigned char *buf2 = (unsigned char *)malloc(5);
        //llread(buf2);
    }
    llclose(0);
}

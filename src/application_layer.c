// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename)
{
    // TODO
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    if (strcmp(role, "tx") == 0) // role == "tx"
        connectionParameters.role = LlTx;
    else if (strcmp(role, "rx") == 0) // role == "rx"
        connectionParameters.role = LlRx;
    else
        return;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    llopen(connectionParameters);
    unsigned char buf[5] = {0x00, 0x01, 0x02, 0x03, 0x04}; //, 0x05, 0x06, 0x07, 0x08, 0xA0};
    llwrite(buf, 5);
}

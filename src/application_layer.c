// Application layer protocol implementation

#include "application_layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "link_layer.h"

#define DATA_PACKET 1
#define CONTROL_PACKET_START 2
#define CONTROL_PACKET_END 3

#define CONTROL_PACKET_FILE_SIZE 0
#define CONTROL_PACKET_FILE_NAME 1

#define MAX_DATA_SIZE 512

void printAL(char *title, unsigned char *content, int contentSize) {
    // DEBUG
    printf("\nApplication Layer\n");
    for (int i = 0; title[i] != '\0'; i++) printf("%c", title[i]);
    for (int i = 0; i < contentSize; i++) printf("0x%x ", content[i]);
    printf("\n");
}

int logaritmo2(int n) {
    int res = -1;
    while (n > 0) {
        n /= 2;
        res++;
    }
    return res;
}

unsigned char *buildControlPacket(unsigned char controlField, long int fileSize, const char *fileName, int *packetSize) {
    int fileSizeLength = (logaritmo2(fileSize) / 8) + 1;  // número de bits necessários para representar o tamanho do ficheiro
    int fileNameLength = strlen(fileName);

    *packetSize = 5 + fileSizeLength + fileNameLength;  // 5 -> C + T1 + L1 + T2 + L2
    unsigned char *controlPacket = (unsigned char *)malloc(*packetSize);

    controlPacket[0] = controlField;
    controlPacket[1] = CONTROL_PACKET_FILE_SIZE;
    controlPacket[2] = fileSizeLength;

    int index;
    for (index = 3; index < (fileSizeLength + 3); index++) {
        unsigned leftmost = fileSize & 0xFF << (fileSizeLength - 1) * 8;
        leftmost >>= (fileSizeLength - 1) * 8;
        fileSize <<= 8;
        controlPacket[index] = leftmost;
    }

    controlPacket[++index] = CONTROL_PACKET_FILE_NAME;
    controlPacket[++index] = fileNameLength;
    memcpy(controlPacket + index, fileName, fileNameLength);

    printAL("Pacote de Controlo Construído: ", controlPacket, *packetSize);  // DEBUG

    return controlPacket;
}

unsigned char *buildDataPacket(int dataSize, unsigned char *data, int *packetSize) {
    *packetSize = dataSize + 3;  // 3 -> C + L2 + L1
    unsigned char *dataPacket = (unsigned char *)malloc(*packetSize);

    dataPacket[0] = DATA_PACKET;
    dataPacket[1] = dataSize / 256;
    dataPacket[2] = dataSize % 256;

    memcpy(dataPacket + 3, data, dataSize);

    printAL("Pacote de Dados Construído: ", dataPacket, *packetSize);  // DEBUG

    return dataPacket;
}

void sendDataPacket(int size, unsigned char *fileContent) {
    unsigned char *data;
    data = (unsigned char *)malloc(size);
    memcpy(data, fileContent, size);

    int dataPacketSize;
    unsigned char *dataPacket = buildDataPacket(size, data, &dataPacketSize);

    if (llwrite(dataPacket, dataPacketSize) < 0) {
        printf("Error sending complete data packet\n");
        exit(-1);
    }

    free(data);
}

void applicationLayer(const char *serialPort, const char *role, int baudRate, int nTries, int timeout, const char *filename) {
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    if (strcmp(role, "tx") == 0)  // role == "tx"
        connectionParameters.role = LlTx;
    else if (strcmp(role, "rx") == 0)  // role == "rx"
        connectionParameters.role = LlRx;
    else {
        printf("Invalid role: %s\n", role);
        exit(-1);
    }

    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    if (llopen(connectionParameters) < 0) {
        printf("Error opening connection\n");
        exit(-1);
    }

    if (connectionParameters.role == LlTx) {
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("Error opening file to read\n");
            exit(-1);
        }

        long int fileSize;
        struct stat st;
        if (stat(filename, &st) == 0) {
            fileSize = st.st_size;
            printf("O tamanho do ficheiro é %ld bytes\n", fileSize);  // DEBUG
        } else {
            printf("Error getting file size\n");
            exit(-1);
        }

        // Enviar pacote de controlo 'start'
        int startControlPacketSize;
        unsigned char *startControlPacket = buildControlPacket(CONTROL_PACKET_START, fileSize, filename, &startControlPacketSize);
        if (llwrite(startControlPacket, startControlPacketSize) < 0) {
            printf("Error sending start control packet\n");
            exit(-1);
        }

        unsigned char *fileContent = (unsigned char *)malloc(fileSize * sizeof(unsigned char));
        fread(fileContent, sizeof(unsigned char), fileSize, file);

        int completePackets = fileSize / MAX_DATA_SIZE;
        int incompletePacketSize = fileSize % MAX_DATA_SIZE;

        // Enviar pacotes de dados 'completos'
        for (int i = 0; i < completePackets; i++) {
            sendDataPacket(MAX_DATA_SIZE, fileContent);
            fileContent += MAX_DATA_SIZE;
        }

        // Enviar pacote de dados 'incompleto'
        if (incompletePacketSize != 0) {
            sendDataPacket(incompletePacketSize, fileContent);
        }

        // Enviar pacote de controlo 'end'
        int endControlPacketSize;
        unsigned char *endControlPacket = buildControlPacket(CONTROL_PACKET_END, fileSize, filename, &endControlPacketSize);
        if (llwrite(endControlPacket, endControlPacketSize) < 0) {
            printf("Error sending end control packet\n");
            exit(-1);
        }

        fclose(file);
        // free(fileContent); // <-- TODO: isto falha por alguma razão
    } else if (connectionParameters.role == LlRx) {
        FILE *newFile = fopen(filename, "wb");
        if (newFile == NULL) {
            printf("Error opening file to write\n");
            exit(-1);
        }

        char *newFileName;
        unsigned char *packet = (unsigned char *)malloc(MAX_DATA_SIZE);
        while (TRUE) {
            if (llread(packet) > 0) {
                printAL("Recetor recebeu: ", packet, sizeof(packet));
                if (packet[0] == CONTROL_PACKET_START) {
                    unsigned char fileSizeLength = packet[2];
                    int fileSize = 0;
                    for (unsigned char i = 3; i < (fileSizeLength + 3); i++) {
                        fileSize |= packet[i];
                        fileSize <<= 8;
                    }

                    unsigned char fileNameLength = packet[fileSizeLength + 5];
                    newFileName = (char *)malloc(fileNameLength);
                    memcpy(newFileName, packet + fileSizeLength + 5, fileNameLength);
                    printf("Started receiving file: %s\n", newFileName);
                } else if (packet[0] == DATA_PACKET) {
                    int dataSize = packet[1] * 256 + packet[2];
                    fwrite(packet + 3, sizeof(unsigned char), dataSize, newFile);
                } else if (packet[0] == CONTROL_PACKET_END) {
                    printf("Ended receiving file: %s\n", newFileName);
                    break;
                }
            }
        }

        fclose(newFile);
        free(packet);
    }

    if (llclose(FALSE) < 0) {
        printf("Error closing connection\n");
        exit(-1);
    }
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include "appLayer/applicationLayer.h"
#include "dataLayer/dataLayer.h"
#include "macros.h"
#include "utils/utils.h"

application app;
int baudrate = BAUDRATE;
extern int timeoutLength;
unsigned maxFrameSize = MAX_FRAME_SIZE;
unsigned maxFrameDataLength = MAX_FRAME_DATA_LENGTH;
unsigned maxPacketLength = MAX_PACKET_LENGTH;
unsigned maxPacketDataLength = MAX_PACKET_DATA_LENGTH;

int main(int argc, char *argv[])
{
    system("umask 0077");

    if (argc == 3 || argc == 6) {
        if ((strcmp("-r", argv[1]) != 0)) {
            printf("Receiver usage: %s -r <port> [baudrate maxFrameSize timeout]\nTransmitter usage: %s -s <port> <filename> [baudrate maxFrameSize timeout]\n", argv[0], argv[0]);
            printf("Valid baudrates are:\n0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200\n");
            exit(1);
        }
    }
    else if (argc == 4 || argc == 7) {
        if ((strcmp("-s", argv[1]) != 0)) {
            printf("Receiver usage: %s -r <port> [baudrate maxFrameSize timeout]\nTransmitter usage: %s -s <port> <filename> [baudrate maxFrameSize timeout]\n", argv[0], argv[0]);
            printf("Valid baudrates are:\n0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200\n");
            exit(1);
        }
    }
    else {
        printf("Receiver usage: %s -r <port> [baudrate maxFrameSize timeout]\nTransmitter usage: %s -s <port> <filename> [baudrate maxFrameSize timeout]\n", argv[0], argv[0]);
        printf("Valid baudrates are:\n0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200\n");
        exit(1);
    }

    if (strcmp("-s", argv[1])== 0) {
        app.status = TRANSMITTER;
        if(argc == 7) {
            baudrate = convertBaudrate(atoi(argv[4]));
            maxFrameSize = atoi(argv[5]);
            if (maxFrameSize > 128000) {
                printf("MAIN - Max value for frame size is 128000. Setting it to default 512...\n");
                maxFrameSize = 512;
            }
            timeoutLength = atoi(argv[6]);
            maxFrameDataLength = (maxFrameSize - 8);
            maxPacketLength = maxFrameDataLength;
            maxPacketDataLength = maxFrameDataLength - 4;
        }
    }
    else if (strcmp("-r", argv[1])== 0) {
        app.status = RECEIVER;
        if(argc == 6) {
            baudrate = convertBaudrate(atoi(argv[3]));
            maxFrameSize = atoi(argv[4]);
            if (maxFrameSize > 128000) {
                printf("MAIN - Max value for frame size is 128000. Setting it to default 512...\n");
                maxFrameSize = 512;
            }
            timeoutLength = atoi(argv[5]);
            maxFrameDataLength = (maxFrameSize - 8);
            maxPacketLength = maxFrameDataLength;
            maxPacketDataLength = maxFrameDataLength - 4;
        }
    }
    
    if (app.status == TRANSMITTER) {
        strcpy(app.port, argv[2]);
        strcpy(app.filename, argv[3]);
    }
    else if (app.status == RECEIVER) {
        strcpy(app.port, argv[2]);
    }

    printf("MAIN - Starting app...\n");

    appRun();

    printf("MAIN - Closing app...\n");

    return 0;
}


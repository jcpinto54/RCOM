#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "dataLayer.h"
#include "dataLayerPrivate.h"

int status;
int timeoutLength;
extern int idFrameSent;
extern int lastFrameReceivedId;
extern int baudrate;

extern int maxFrameSize;
extern int maxFrameDataLength;


int llopen(char *port, int appStatus)
{
    printf("DATA - Entered llopen\n");
    status = appStatus;

    struct termios oldtio, newtio;

    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(port);
        return -1;
    }

    if (tcgetattr(fd, &oldtio) == -1) {
        perror("tcgetattr");
        return -2;
    }


    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = baudrate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; // time to time-out in deciseconds
    newtio.c_cc[VMIN] = 1;  // min number of chars to read

    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        return -3;
    }

    frame_t setFrame;
    frame_t responseFrame;
    frame_t receiverFrame;
    frame_t uaFrame;
    setFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));
    responseFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));
    receiverFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));
    uaFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));

    (*(setFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(responseFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(receiverFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(uaFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);


    switch (appStatus) {
        case TRANSMITTER:;
            for (int i = 0;; i++) {
                if (i == MAX_FRAME_RETRANSMISSIONS) {
                    printf("DATA - Max Number of retransmissions reached. Exiting program.\n");
                    return -1;
                }

                buildSETFrame(&setFrame, true);
            
                if (sendNotIFrame(&setFrame, fd)) {
                    perror("sendNotIFrame\n");
                    return -5;
                }
                prepareToReceive(&responseFrame, 5);
                int responseReceive = receiveNotIMessage(&responseFrame, fd, RESPONSE_WITHOUT_ID, timeoutLength); 
                

                if (responseReceive == -1) continue;         // in a timeout, retransmit frame
                else if (responseReceive < -2) {printf("Error in receiveNotIMessage from llopen\n");  return -7;}
                if (!isUAFrame(&responseFrame)) continue;       // wrong frame received

                break;
            }
            break;
        case RECEIVER:;
            prepareToReceive(&receiverFrame, 5);
            int error = receiveNotIMessage(&receiverFrame, fd, RESPONSE_WITHOUT_ID, NO_TIMEOUT);
            if (error) {
                printf("DATA - ReceiveNotIMessage returned %d\n", error); 
                return -7;
            }
            if (!isSETFrame(&receiverFrame)) {
                printf("DATA - Frame is not of type SET\n"); 
                return -8;
            }

            buildUAFrame(&uaFrame, true);
            if (sendNotIFrame(&uaFrame, fd)) {
                printf("DATA - Problem in sendNotIFrame\n"); 
                return -5;
            }
            break;
    }
    printf("DATA - Opened serial port connection\n");

    return fd;
}

int llclose(int fd) {
    printf("DATA - Entered llclose\n");
    frame_t discFrame;
    frame_t receiveFrame;
    frame_t uaFrame;
    
    discFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));
    receiveFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));
    uaFrame.bytes = (u_int8_t **)malloc(sizeof(u_int8_t *));

    (*(discFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(receiveFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(uaFrame.bytes)) = (u_int8_t *)malloc(maxFrameSize);


    int receiveReturn;
    switch (status) {
        case TRANSMITTER:;
            for (int i = 0;; i++) {
                if (i == MAX_FRAME_RETRANSMISSIONS) {
                    printf("DATA - Max Number of retransmissions reached. Exiting program.\n");
                    return -1;
                }

                buildDISCFrame(&discFrame, true);
                if (sendNotIFrame(&discFrame, fd)) return -2;

                prepareToReceive(&receiveFrame, 5);
                receiveReturn = receiveNotIMessage(&receiveFrame, fd, RESPONSE_WITHOUT_ID, timeoutLength);
                if (receiveReturn == -1) continue;        //in a timeout, retransmit frame
                else if (receiveReturn < -1) return -4;
                if (!isDISCFrame(&receiveFrame)) continue;      // wrong frame received

                buildUAFrame(&uaFrame, true);
                if (sendNotIFrame(&uaFrame, fd)) return -2;
                
                break;
            }
        break;
        case RECEIVER:;
            for (int i = 0; i < MAX_READ_ATTEMPTS; i++) {
                prepareToReceive(&receiveFrame, 5);
                int receiveReturn = receiveNotIMessage(&receiveFrame, fd, RESPONSE_WITHOUT_ID, timeoutLength);
                if (receiveReturn == -1) continue;
                else if (receiveReturn) return -7;
                if (!isDISCFrame(&receiveFrame)) return -5;
                break;
            }
            buildDISCFrame(&discFrame, true);
            if (sendNotIFrame(&discFrame, fd)) return -2;
            prepareToReceive(&receiveFrame, 5);
            int receiveNotIMessageReturn = receiveNotIMessage(&receiveFrame, fd, RESPONSE_WITHOUT_ID, NO_TIMEOUT);
            if (receiveNotIMessageReturn) return -4;
            if (!isUAFrame(&receiveFrame)) return -5;
        break;
    }
    if (close(fd) == -1) return -8;
    printf("DATA - Closed serial port connection\n");
    return 1;
}


int llread(int fd, char * buffer){
    frame_t frame, response;
    frame.bytes = (u_int8_t **) malloc(sizeof(u_int8_t *));
    response.bytes = (u_int8_t **) malloc(sizeof(u_int8_t *));
    (*(frame.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    (*(response.bytes)) = (u_int8_t *)malloc(maxFrameSize);
    int receiveIMessageReturn, sameReadAttempts = 1;

    do {
        receiveIMessageReturn = receiveIMessage(&frame, fd);
        if (receiveIMessageReturn < -3 || receiveIMessageReturn > 1) {
            printf("DATA - receiveIMessage returned unexpected value\n");
            return -1;
        }
        if (receiveIMessageReturn >= 0) {
            prepareResponse(&response, true, (frame.infoId + 1) % 2);
            printf("DATA - Sent RR frame to the transmitter\n");
            sameReadAttempts = 0;
        }
        else if (receiveIMessageReturn == -1 || receiveIMessageReturn == -2) {
            if (lastFrameReceivedId != -1 && frame.infoId == lastFrameReceivedId) {
                prepareResponse(&response, true, (frame.infoId + 1) % 2);
                printf("DATA - Read a duplicate frame\nDATA - Sent RR frame to the transmitter\n");
                sameReadAttempts = 0;
            }
            else { 
                prepareResponse(&response, false, (frame.infoId + 1) % 2);
                printf("DATA - Sent REJ frame to the transmitter\n");
                sameReadAttempts++;
            }
        }
        if (receiveIMessageReturn >= 0) {
            
            lastFrameReceivedId = frame.infoId;
        }
        if (receiveIMessageReturn != -3) {
            if (sendNotIFrame(&response, fd) == -1) return -1;
        }
        if (receiveIMessageReturn == 0){
            memcpy(buffer, (*(frame.bytes)) + 6, (*(frame.bytes))[4] * 256 + (*(frame.bytes))[5]);      
            
        }
              

        if (receiveIMessageReturn == -3) {
            printf("DATA - Serial Port couldn't be read. Exiting llread...\n");
            return -1;
        }
    } while (receiveIMessageReturn != 0 && sameReadAttempts < MAX_READ_ATTEMPTS);

    if (sameReadAttempts == MAX_READ_ATTEMPTS) {
        printf("DATA - Max read attempts of the same frame reached.\n");
        return -1;
    }
    return 0;
}

int llwrite(int fd, char * buffer, int length)
{

    frame_t info;
    prepareI(&info, buffer, length); //Prepara a trama de informação

    if (sendIFrame(&info, fd) == -1) return -1;
    return 0;
}


int clearSerialPort(char *port) {
    printf("Clearing serial port in case of errors. Quit program with Ctrl-C\n");

    int auxFd = open(port, O_RDWR | O_NOCTTY);
    if (auxFd == -1) {
        perror("DATA - error clearing serialPort");
        return 1;
    }

    struct termios oldtio, newtio;

    if (tcgetattr(auxFd, &oldtio) == -1) {
        perror("tcgetattr");
        return -2;
    }


    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; // time to time-out in deciseconds
    newtio.c_cc[VMIN] = 1;  // min number of chars to read

    if (tcsetattr(auxFd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        return -3;
    }

    char c;
    while (read(auxFd, &c, 1) != 0) printf("DATA - byte cleared: %x\n", c);
    if (close(auxFd) == -1) return 2;
    return 0;
}


int convertBaudrate(int baudArg) {
    switch(baudArg) {
        case 0:         return 0000000;		/* hang up */
        case 50:        return 0000001;
        case 75:        return 0000002;
        case 110:       return 0000003;
        case 134:       return 0000004;
        case 150:       return 0000005;
        case 200:       return 0000006;
        case 300:       return 0000007;
        case 600:       return 0000010;
        case 1200:      return 0000011;
        case 1800:      return 0000012;
        case 2400:      return 0000013;
        case 4800:      return 0000014;
        case 9600:      return 0000015;
        case 19200:     return 0000016;
        case 38400:     return 0000017;
        case 57600:     return 0010001;
        case 115200:    return 0010002;
        default: 
            printf("\nArgument is not a valid baudrate. Using default Baudrate 38400\n");
            printf("Valid baudrates are:\n0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200\n\n");
            break;
    }
    return 0000017;
}

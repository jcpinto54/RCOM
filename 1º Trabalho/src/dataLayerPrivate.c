#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "dataLayer.h"
#include "dataLayerPrivate.h"
#include "macros.h"
#include "utils.h"
extern applicationLayer application;

int idFrameSent = 0;
int idFrameResponse = 1;

void auxStuffing(frame_t * frame, int * stuffingCounter, char *data)
{
    for(unsigned int i = 0; i < frame->bytes[4]; i++) {
        if(data[i] == FLAG){//do byte stuffing
            frame->bytes[5 + i + (*stuffingCounter)] = ESC;
            frame->bytes[5 + i + (++(*stuffingCounter))] = FLAG_STUFFING;
        }
        else if(data[i] == ESC){//do byte stuffing
            frame->bytes[5 + i + (*stuffingCounter)] = ESC;
            frame->bytes[5 + i + (++(*stuffingCounter))] = ESC_STUFFING;
        }
        else{
            frame->bytes[5 + i + (*stuffingCounter)] = data[i];
        }
    }
}

// pode ser necessário ter os dados em mais que uma frame
int prepareI(char* data, int length, frame_t *** infoNew) //Testar
{
    int framesNeeded = ceiling(1.0/(MAX_FRAME_DATA_LENGTH/(float)length));      // Division has got to be like this   

    u_int8_t frameDataSize;
    frame_t **info = malloc(sizeof(frame_t *) * framesNeeded);
    for (int i = 0; i < framesNeeded; i++) {
        info[i] = malloc(sizeof(frame_t));

        info[i]->bytes[0] = FLAG; //F
        info[i]->bytes[1] = TRANSMITTER_TO_RECEIVER; //A
        info[i]->bytes[2] = idFrameSent << 6 | I;
        info[i]->bytes[3] = bccCalculator(info[i]->bytes, 1, 2); //BCC1, calculado com A e C
    
        int stuffingCounter = 0;
        //Talvez colocar o tamanho da mensagem como primeiro byte?

        unsigned lengthInOtherFrames = 0;
        if (i < framesNeeded - 1 && framesNeeded != 1) frameDataSize = MAX_FRAME_DATA_LENGTH;
        else {
            for (int j = 0; j < i; j++) {
                lengthInOtherFrames += info[j]->bytes[4];
            }
            frameDataSize = length - lengthInOtherFrames;
        }
        info[i]->bytes[4] = frameDataSize;

        auxStuffing(info[i], &stuffingCounter, data);
        int bcc2_byte_ix = 4 + 1 + frameDataSize + stuffingCounter + 1;

        if (i == framesNeeded - 1) {
            info[i]->bytes[bcc2_byte_ix - 1] = FLAG_LAST_FRAME;        
        }
        else {
            info[i]->bytes[bcc2_byte_ix - 1] = FLAG_MORE_FRAMES_TO_COME;
        }
        
        info[i]->bytes[bcc2_byte_ix] = bccCalculator(info[i]->bytes, 4, frameDataSize + 1);   // +1 because of the byte to know wether this is the last frame
        info[i]->bytes[bcc2_byte_ix + 1] = FLAG;
        info[i]->size = 8 + frameDataSize + stuffingCounter;
        data += frameDataSize;
    }
    *infoNew = info;
    return framesNeeded;
}

int receiveIMessage(frame_t *frame){
    u_int8_t c;
    receive_state_t state = INIT;
    int dataCounter = 0;
    do {
        int bytesRead = read(application.fd, &c, 1);
        if (bytesRead < 0) {
            perror("read error");
            return -1;
        }

        switch (state) {
            case INIT:
                if (c == FLAG) {
                    state = RCV_FLAG;
                    frame->bytes[0] = c;
                }
                break;
            case RCV_FLAG:
                if (c == TRANSMITTER_TO_RECEIVER || c == RECEIVER_TO_TRANSMITTER) {
                    state = RCV_A;
                    frame->bytes[1] = c;
                }
                else
                    state = INIT;
                break;
            case RCV_A:
                if (c == (idFrameSent << 6)) {
                    state = RCV_C;
                    frame->bytes[2] = c;
                    idFrameSent = (idFrameSent+1) % 2;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                else
                    state = INIT;
                break;
            case RCV_C:
                if (bccVerifier(frame->bytes, 1, 2, c)) {
                    state = RCV_BCC1;
                    frame->bytes[3] = c;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                else {
                    printf("BCC1 not correct\n");
                    return -1;
                }
                break;
            case RCV_BCC1:      // needs destuffing
                if (dataCounter == 0 && c > 0 && c <= MAX_FRAME_DATA_LENGTH) {
                    frame->bytes[4] = c;
                }
                else if (dataCounter == 0 && (c < 0 || c > MAX_FRAME_DATA_LENGTH)) return -1;
                if (c == FLAG) {     
                    state = RCV_FLAG;
                    continue;
                }
                frame->bytes[4 + dataCounter] = c;
                dataCounter++;
                if (dataCounter == frame->bytes[4]) state = RCV_DATA;
                break;
            case RCV_DATA:
                if (c == FLAG_MORE_FRAMES_TO_COME || c == FLAG_LAST_FRAME) {
                    state = RCV_LAST_FRAME_FLAG;
                    frame->bytes[4 + dataCounter + 1] = c;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                break;            
            case RCV_LAST_FRAME_FLAG:
                if (bccVerifier(frame->bytes, 4, frame->bytes[4] + 1, c)) {
                    state = RCV_BCC2;
                    frame->bytes[4 + dataCounter + 2] = c;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                else {
                    printf("BCC2 not correct\n");
                    return -1;
                }
                break;
            case RCV_BCC2:
                if (c == FLAG) {
                    state = COMPLETE;
                    frame->bytes[4 + dataCounter + 3] = c;
                }
                else
                    state = INIT;
                break;
            case COMPLETE: break;
        }
    } while (state != COMPLETE);


    if (frame->bytes[4 + dataCounter + 1] == FLAG_MORE_FRAMES_TO_COME) return 1;
    else if (frame->bytes[4 + dataCounter + 1] == FLAG_LAST_FRAME) return 0;
    
    return -1;
}

int receiveNotIMessage(frame_t *frame)
{
    u_int8_t c;
    receive_state_t state = INIT;
    time_t initTime, curTime;
    initTime = time(NULL);
    do {
        int bytesRead = read(application.fd, &c, 1);
        if (bytesRead < 0) {
            perror("read error");
            return 3;
        }
        else if (bytesRead == 0) {
            curTime = time(NULL);
            time_t seconds = curTime - initTime;
            if (seconds >= TIMEOUT) {
                printf("Read Timeout!\n");
                return 1;
            }
        }
        else if (bytesRead > 0) {
            initTime = time(NULL);
        }

        switch (state) {
            case INIT:
                if (c == FLAG) {
                    state = RCV_FLAG;
                    frame->bytes[0] = c;
                }
                break;
            case RCV_FLAG:
                if (c == TRANSMITTER_TO_RECEIVER || c == RECEIVER_TO_TRANSMITTER) {
                    state = RCV_A;
                    frame->bytes[1] = c;
                }
                else
                    state = INIT;
                break;
            case RCV_A:
                if (c == SET || c == UA || c == DISC || c == RR || c == REJ) {
                    state = RCV_C;
                    frame->bytes[2] = c;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                else
                    state = INIT;
                break;
            case RCV_C:
                if (bccVerifier(frame->bytes, 1, 2, c)) {
                    state = RCV_BCC1;
                    frame->bytes[3] = c;
                }
                else if (c == FLAG)
                    state = RCV_FLAG;
                else {
                    perror("BCC1 not correct\n");
                    return 2;
                }
                break;
            case RCV_BCC1:
                if (c == FLAG) {
                    state = COMPLETE;
                    frame->bytes[4] = c;
                }
                else
                    state = INIT;
                break;
            case COMPLETE:
                break;
            default:
                perror("Unknown state");
                break;
        }
    } while (state != COMPLETE);
    
    return 0;
}

int sendNotIFrame(frame_t *frame) {
    int sentBytes = 0;

    for (int attempts = 0;(sentBytes != frame->size); attempts++) {
        if (attempts >= MAX_WRITE_ATTEMPTS) {
            perror("Too many failed attempts to send. Time out!\n");
            return -1;
        }
        if ((sentBytes = write(application.fd, frame->bytes, frame->size)) == -1) return -1; 
        printf("%d bytes sent\n", sentBytes);
    }
    return 0;
}

int sendIFrame(frame_t *frame) {
    int attempts = 0, sentBytes = 0;
    frame_t responseFrame;
    while (1) {
        if(attempts >= MAX_WRITE_ATTEMPTS) 
        {
            perror("ERROR: Too many write attempts\n");
            return -1;
        }
    
        if ((sentBytes = write(application.fd, frame->bytes, frame->size)) == -1) return -1;                  
        printf("%d bytes sent\n", sentBytes);

        if (receiveNotIMessage(&responseFrame) == 1) {
            printf("Timeout reading response, trying again...\n");
            attempts++;
            continue;
        }

        if(responseFrame.bytes[2] == (RR | ((idFrameResponse + 1) % 2 << 7))) {
            break;
        }
        else if(responseFrame.bytes[2] == (REJ | ((idFrameResponse + 1) % 2 << 7))) {
            attempts++;
        }
        else {
            perror("ERROR: Unknown response frame");
        }
    }
    return 0;
}


// ---

u_int8_t bccCalculator(u_int8_t bytes[], int start, size_t length)
{
    int onesCounter = 0;
    for (int i = start; i < start + length; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            onesCounter += getBit(bytes[i], j);
        }
    }
    return onesCounter % 2;
}

bool bccVerifier(u_int8_t bytes[], int start, size_t length, u_int8_t parity)
{
    if (bccCalculator(bytes, start, length) == parity)
        return true;
    return false;
}

void buildSETFrame(frame_t *frame, bool transmitterToReceiver)
{
    frame->size = 5;
    frame->bytes[0] = FLAG;
    if (transmitterToReceiver)
        frame->bytes[1] = TRANSMITTER_TO_RECEIVER;
    else
        frame->bytes[1] = RECEIVER_TO_TRANSMITTER;
    frame->bytes[2] = SET;
    frame->bytes[3] = 0;    // BCC
    frame->bytes[4] = FLAG;
}

bool isSETFrame(frame_t *frame) {
    if (frame->size != 5) return false;
    return frame->bytes[2] == SET;
}

void buildUAFrame(frame_t *frame, bool transmitterToReceiver)
{
    frame->size = 5;
    frame->bytes[0] = FLAG;
    if (transmitterToReceiver)
        frame->bytes[1] = TRANSMITTER_TO_RECEIVER;
    else
        frame->bytes[1] = RECEIVER_TO_TRANSMITTER;
    frame->bytes[2] = UA;
    frame->bytes[3] = 1;    // BCC
    frame->bytes[4] = FLAG;
}

bool isUAFrame(frame_t *frame) {
    if (frame->size != 5) return false;
    return frame->bytes[2] == UA;
}

void buildDISCFrame(frame_t *frame, bool transmitterToReceiver)
{
    frame->size = 5;
    frame->bytes[0] = FLAG;
    if (transmitterToReceiver)
        frame->bytes[1] = TRANSMITTER_TO_RECEIVER;
    else
        frame->bytes[1] = RECEIVER_TO_TRANSMITTER;
    frame->bytes[2] = DISC;
    frame->bytes[3] = 1;    // BCC
    frame->bytes[4] = FLAG;
}

bool isDISCFrame(frame_t *frame) {
    if (frame->size != 5) return false;
    return frame->bytes[2] == DISC;
}

void prepareToReceive(frame_t *frame, size_t size)
{
    frame->size = size;
}

void printFrame(frame_t *frame) {
    printf("\nStarting printFrame...\n\tSize: %ld\n", frame->size);
    for (int i = 0; i < frame->size; i++)
    {
        printf("\tByte %d: %x \n", i, frame->bytes[i]);
    }
    printf("\nprintFrame ended\n");
}


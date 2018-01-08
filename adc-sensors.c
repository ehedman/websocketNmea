/*
 * adc-sensors.c
 *
 *  Copyright (C) 2013-2018 by Erland Hedman <erland@hedmanshome.se>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <termios.h> 
#include <unistd.h>
#include <errno.h>
#include "wsocknmea.h"


#ifdef UK1104   // https://www.canakit.com/

enum modes {
    DigIn = 1,
    DigOut,
    AnaogIn,
    TempIn
};

#define UK1104P         "\r\n"
#define PUK1104         "::"
#define CHxSETMOD       "CH%d.SETMOD%d\r\n"
#define CHxGETANALOG    "CH%d.GETANALOG\r\n"
#define CHxGETTEMP      "CH%d.GETTEMP\r\n"
#define RELxON          "REL%d.ON\r\n"
#define RELxOFF         "REL%d.OFF\r\n"
#define RELxGET         "REL%d.GET\r\n"
#define RELSGET         "RELS.GET\r\n"
#define ON              1
#define OFF             0

#define ADBZ    24
#define IOMAX   10      // 6+4
#define RELCHA  5
#define MAXTRY  4
#define IOWAIT  350000
#define MAXAGE  20

#define CHAisNOTUSED    0
#define CHAisCLAIMED    1
#define CHAisREADY      2

static struct serial {
    int fd;
} serialDev;

struct adData
{
    char adBuffer[ADBZ];
    float curVal;
    int mode;
    int status;
    time_t age;
};

static int runThread = ON;

static struct adData adChannel[IOMAX];

static int getPrompt(void)
{
    char buffer[ADBZ];
    size_t cnt;
    int rval = 1;
    extern int errno;

    //(void)usleep(IOWAIT*2); return 0;

    if (!serialDev.fd) return 1;

    for (int i = 0; i < 3; i++) 
    {
        (void)tcflush(serialDev.fd, TCIFLUSH);

        (void)sprintf(buffer, UK1104P);
        cnt = write(serialDev.fd, buffer, strlen(buffer)); // Write the UK1104 init wake up string
        if (cnt != strlen(buffer)) {
            printlog("UK1104: Error in sending get prompt string");
            return rval;
        }

        (void)memset(buffer, 0 , ADBZ);
        for (int try = 0; try < MAXTRY; try++)
        {
            (void)usleep(IOWAIT);
            cnt = read(serialDev.fd, buffer, ADBZ); // Get the response
            if (cnt == -1 && errno == EAGAIN) continue; 
            if (cnt > 1 && !strncmp(buffer, PUK1104, 2)) {  // printf("b=%s,try=%d,i=%d\n", buffer,try ,i);
                rval = 0;
                break;
            }
        }
        if (rval == 0) break;
    }

    (void)tcflush(serialDev.fd, TCIFLUSH);

    return rval;
}

static void executeCommand(char *cmdFmt, int chn)
{
    char rbuf[ADBZ];
    size_t cnt;

    if (getPrompt()) {
        printlog("UK1104: Error in getting the ready prompt");
        return;
    }

    cnt = write(serialDev.fd, cmdFmt, strlen(cmdFmt));  // Write the command string
    if (cnt != strlen(cmdFmt)) {
        printlog("UK1104: Error in sending command %s", cmdFmt);
        return;
    } else {
        (void)memset(adChannel[chn].adBuffer, 0 , ADBZ);
        (void)memset(rbuf, 0 , ADBZ);

        for (int try = 0; try < MAXTRY; try++)
        {
            (void)usleep(IOWAIT); //printf("RETRY %s %d\n", rbuf, cnt);
            cnt = read(serialDev.fd, rbuf, ADBZ); // Get the value
            if (cnt == -1 && errno == EAGAIN) continue;
            if (adChannel[chn].status == CHAisCLAIMED && cnt > 1) {
                adChannel[chn].status = CHAisREADY;
                break;
            }
            if (cnt >0) {
                adChannel[chn].age = time(NULL);
                (void)strcat(adChannel[chn].adBuffer, rbuf);
                //printf("catting=%s <- %s, try=%d\n", adChannel[chn].adBuffer, rbuf, try);
            }
        }
    }
    (void)tcflush(serialDev.fd, TCIFLUSH);
    //if (strlen(adChannel[chn].adBuffer)) printf("got %s\n", adChannel[chn].adBuffer);
}

// Reader thread
void *t_devMgm()
{
    static char cmdFmt[ADBZ];
    int chn;

    while(ON)
    {
        if (runThread == OFF) {
            sleep(1);
            continue;
        }

        for (chn = 0; chn < IOMAX; chn++)
        {
            if (adChannel[chn].status == CHAisNOTUSED)
                continue;

            if (chn > RELCHA) // realys
            {
                if (adChannel[chn].mode == ON)
                    sprintf(cmdFmt, RELxON, chn-RELCHA);
                else
                    sprintf(cmdFmt, RELxOFF, chn-RELCHA);
            } else {
                if (adChannel[chn].status == CHAisCLAIMED) {
                    sprintf(cmdFmt, CHxSETMOD, chn, adChannel[chn].mode);
                } else {               
                    switch (adChannel[chn].mode)
                    {
                        case AnaogIn:
                            sprintf(cmdFmt, CHxGETANALOG, chn);
                        break;
                        case TempIn:
                            sprintf(cmdFmt, CHxGETTEMP, chn);
                        break;
                        default:
                        break;
                    }              
                }
            }
            executeCommand(cmdFmt, chn);
        }
    }
    /* NOT REACHED */
}

static int portConfigure(int fd, char *device)
{

    struct termios SerialPortSettings;
    static pthread_attr_t attr;  
    static pthread_t t1;
    int detachstate;

    if (serialDev.fd) return 0;

    (void)tcgetattr(fd, &SerialPortSettings);     // Get the current attributes of the Serial port

    /* Setting the Baud rate */
    (void)cfsetispeed(&SerialPortSettings,B115200); // Set Read  Speed as 15200
    (void)cfsetospeed(&SerialPortSettings,B115200); // Set Write Speed as 15200

    /* 8N1 Mode */
    SerialPortSettings.c_cflag &= ~PARENB;  // Disables the Parity Enable bit(PARENB),So No Parity
    SerialPortSettings.c_cflag &= ~CSTOPB;  // CSTOPB = 2 Stop bits,here it is cleared so 1 Stop bit
    SerialPortSettings.c_cflag &= ~CSIZE;   // Clears the mask for setting the data size
    SerialPortSettings.c_cflag |=  CS8;     // Set the data bits = 8

    SerialPortSettings.c_cflag &= ~CRTSCTS;         // No Hardware flow Control
    SerialPortSettings.c_cflag |= CREAD | CLOCAL;   // Enable receiver,Ignore Modem Control lines


    SerialPortSettings.c_iflag &= ~(IXON | IXOFF | IXANY);  // Disable XON/XOFF flow control both i/p and o/p
    SerialPortSettings.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Non Cannonical mode
    
    SerialPortSettings.c_oflag &= ~OPOST;   // No Output Processing

    // Setting Time outs
    //SerialPortSettings.c_cc[VMIN] = 2;     // Read at least 2 characters 
    //SerialPortSettings.c_cc[VTIME] = 10;  // Wait 1 sec


    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) {  // Set the attributes to the termios structure
        printlog("UK1104: Error in setting serial attributes");
        return 1;
    } else
        printlog("UK1104: %s: BaudRate = 15200, StopBits = 1,  Parity = none", device);

    (void)tcflush(fd, TCIFLUSH);  // Discards old data in the rx buffer

    (void)memset(adChannel, 0, sizeof(adChannel));
    pthread_attr_init(&attr);
    detachstate = PTHREAD_CREATE_DETACHED;
    (void)pthread_attr_setdetachstate(&attr, detachstate);
    (void)pthread_create(&t1, &attr, t_devMgm, NULL); // Start the reader thread

    return 0;
}

/* API */
int adcInit(char *device, int a2dChannel)
{
    int fd;

    if (a2dChannel > IOMAX-1) {
        printlog("UK1104: Error channel must be less than %d not  %d", IOMAX, a2dChannel+1);
        return 1;
    }
    
    if (adChannel[a2dChannel].status) {
        printlog("UK1104: ADC Channel %d already claimed", a2dChannel);
        return 0;
    }

    if (!serialDev.fd) {
        if ((fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY )) <0) {
            printlog("UK1104: ADC Could not open device %s", device);
            return 1;
        }
    }

    if (portConfigure(fd, device)) {
        (void)close(fd);
        serialDev.fd = 0;
        return 1;
    }
    serialDev.fd = fd;

   (void)fcntl(serialDev.fd, F_SETFL, FNDELAY);  // Non-blockning   

    adChannel[a2dChannel].mode = a2dChannel == TPMCH? TempIn : AnaogIn;
    adChannel[a2dChannel].status = CHAisCLAIMED;

    return 0;
}

/* API */
float adcRead(int a2dChannel)
{

    if (adChannel[a2dChannel].status != CHAisREADY) {
        //printlog("ADC: Channel %d not in initialized", a2dChannel);
        return 0;
    }

    if (strlen(adChannel[a2dChannel].adBuffer)) {
        //printlog("received=%s\n", adChannel[a2dChannel].adBuffer);
        adChannel[a2dChannel].curVal = atof(adChannel[a2dChannel].adBuffer);
    }

    if (adChannel[a2dChannel].age < time(NULL) - MAXAGE)
        adChannel[a2dChannel].curVal = 0;   // Zero out aged values

    return adChannel[a2dChannel].curVal;
}

/* API */
void relaySet(int channels) // A bitmask
{
    int i, iter;

    for (i=1, iter=1; i<15; i<<=1, iter++)
    {
        if (channels & i) {
            //printf("Flag: %d set\n", iter);
            adChannel[iter+RELCHA].mode = ON;
        } else {
            //printf("Flag: %d is not set\n", iter);
            adChannel[iter+RELCHA].mode = OFF;
        }
    }
}

/* API */
void relayInit(int nchannels)
{
    int cha = 0;

    if (!serialDev.fd) {
        printlog("UK1104: relayInit: Device not initialized with adcInit()");
        return;
    }

    if (nchannels > 4) {
        printlog("UK1104: relayInit: Relays set to 4, not %d", nchannels);
        nchannels = 4;
    }

    // Get the real status in case of a warm restart
    runThread = OFF;
    sleep(2);

    executeCommand(RELSGET, RELCHA+1);

    cha = atoi(adChannel[RELCHA+1].adBuffer); //printf("RELSGET=%s, %d\n",adChannel[RELCHA+1].adBuffer,cha);
    relaySet(cha);

    runThread = ON;
}

/* API */
int relayStatus(void)
{
    int i, iter;
    int result = 0;

    for (i=1, iter=1; i<15; i<<=1, iter++)
    {
        if (adChannel[iter+RELCHA].mode == ON)
            result |= i;
    }
    return result;  // A bitmask
}


#ifdef TBD
void ioPinInit(int channel, int direction) {}
void ioPinset(int channel, bool level) {}
int ioPinGet(int channel) {}
#endif

#endif

#ifdef MCP3208  // http://robsraspberrypi.blogspot.se/2016/01/raspberry-pi-adding-analogue-inputs.html
#ifndef MCP3208
/*
 * ADC code for MCP3208 SPI Chip 12 bit ADC
 * This code has been tested on an RPI 3
 */

static struct spi {
    int spiFd;
    unsigned char mode;
    unsigned char bitsPerWord;
    unsigned int speed;
    int status;
} spiDev;

static int spiOpen(char * devspi)
{
     if ((spiDev.spiFd = open(devspi, O_RDWR)) <0) {
          printlog("ADC: Could not open SPI device %s", devspi);
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MODE, &spiDev.mode) < 0) {
          printlog("ADC: Could not set SPIMode (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MODE, &spiDev.mode) <0) {
          printlog("ADC: Could not set SPIMode (RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_BITS_PER_WORD, &spiDev.bitsPerWord) <0) {
          printlog("ADC: Could not set SPI bitsPerWord (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_BITS_PER_WORD, &spiDev.bitsPerWord) < 0) {
          printlog("ADC: Could not set SPI bitsPerWord(RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &spiDev.speed) < 0) {
          printlog("ADC: Could not set SPI speed (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &spiDev.speed) <0) {
          printlog("ADC: Could not set SPI speed (RD)...ioctl fail");
          return 1;
     }
     return 0;
}

// Init ADC
static int mcp3208SpiInit(char *devspi, unsigned char spiMode, unsigned int spiSpeed, unsigned char spibitsPerWord)
{
     spiDev.mode = spiMode ;
     spiDev.bitsPerWord = spibitsPerWord;
     spiDev.speed = spiSpeed;
     spiDev.spiFd= -1;

     return spiOpen(devspi);
}

int adcInit(char *device, int a2dChannel)
{
    // check channel for adc
    if (a2dChannel > 7) {
        printlog("ADC: Channel must be less than 8 not  %d", a2dChannel);
        return 1;
    }

    if (!spiDev.status) {
        // Initialize ADC device
        if ( mcp3208SpiInit(device, SPI_MODE_0, 1000000, 8) ) {
            spiDev.status = 0;
            return 1;
        }
    }
    spiDev.status = 1;
    return 0;
}

/********************************************************************
 * This function writes data "data" of length "length" to the spidev
 * device. Data shifted in from the spidev device is saved back into
 * "data".
 * ******************************************************************/
static int spiWriteRead( unsigned char *data, int length){

     struct spi_ioc_transfer spi[length];
     int i;

     // one spi transfer for each byte

     for (i = 0 ; i < length ; i++) {
          memset(&spi[i], 0, sizeof (spi[i]));
          spi[i].tx_buf        = (unsigned long)(data + i); // transmit from "data"
          spi[i].rx_buf        = (unsigned long)(data + i) ; // receive into "data"
          spi[i].len           = sizeof(*(data + i)) ;
          spi[i].delay_usecs   = 0 ;
          spi[i].speed_hz      = spiDev.speed ;
          spi[i].bits_per_word = spiDev.bitsPerWord ;
          spi[i].cs_change = 0;
     }

     return ioctl (spiDev.spiFd, SPI_IOC_MESSAGE(length), &spi);
}

float adcRead(int a2dChannel)
{
    int a2dVal;
    unsigned char data[3];
    
    if (!spiDev.status)
        return 0;

    a2dVal = 0;

    // Do A/D conversion
    data[0] = 0x06 | ((a2dChannel & 0x07) >> 7);
    data[1] = ((a2dChannel & 0x07) << 6);
    data[2] = 0x00;

    (void)spiWriteRead(data, sizeof(data));

    data[1] = 0x0F & data[1];
    a2dVal = (data[1] << 8) | data[2]; 

    return (float)(a2dVal);
}
#endif
#endif




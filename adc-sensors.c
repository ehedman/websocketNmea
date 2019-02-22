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
#ifdef DOADC
#include "stables.h"


#ifdef UK1104   // https://www.canakit.com/

static struct serial {
    int fd;
} serialDev;

struct adData
{
    char adBuffer[ADBZ];
    int curVal;
    int mode;
    int type;
    int status;
    time_t age;
};

static struct adData adChannel[IOMAX];

static int runThread = ON;

static int getPrompt(void)
{
    char buffer[ADBZ];
    int rval = 1;
    extern int errno;
    size_t cnt;
    size_t pz = (size_t)sizeof(PUK1104)-1;

    if (!serialDev.fd) return 1;

    for (int i = 0; i < 3; i++) 
    {
        (void)tcflush(serialDev.fd, TCIOFLUSH);

        (void)sprintf(buffer, UK1104P);
        cnt = write(serialDev.fd, buffer, strlen(buffer)); // Write the UK1104 init wake up string
        if (cnt != strlen(buffer)) {
            printf("UK1104: Error in sending the get prompt string");
            return rval;
        }

        (void)memset(buffer, 0 , ADBZ);

        for (int try = 0; try < MAXTRY; try++)
        {
            (void)usleep(IOWAIT);
            cnt = read(serialDev.fd, buffer, ADBZ); // Get the response
            if (cnt == -1 && errno == EAGAIN) continue;
            if (cnt >= pz && !strncmp(&buffer[strlen(buffer)-pz], PUK1104, pz)) {
                rval = 0;
                break;
            }
        }
        if (rval == 0) break;
    }

    return rval;
}

static void executeCommand(char *cmdFmt, int chn)
{
    char rbuf[ADBZ];
    size_t scnt, cnt;

    if (getPrompt()) {
        printlog("UK1104: Error in getting the ready prompt");
        return;
    }

    (void)memset(adChannel[chn].adBuffer, 0 , ADBZ);

    scnt = write(serialDev.fd, cmdFmt, strlen(cmdFmt));  // Write the command string
    if (scnt != strlen(cmdFmt)) {
        printlog("UK1104: Error in sending command %s", cmdFmt);
        return;
    }

    if (adChannel[chn].type == DigOut || adChannel[chn].type == DigIn) {
        if (adChannel[chn].status == CHAisCLAIMED)
            adChannel[chn].status = CHAisREADY;
        if (adChannel[chn].type == DigOut)
            return;
    }

    (void)memset(rbuf, 0 , ADBZ);

    // Now read the response from the device
    for (int try = 0; try < MAXTRY; try++)
    {
        (void)usleep(IOWAIT);
        cnt = read(serialDev.fd, rbuf, ADBZ); // Get the value
        if (cnt == -1 && errno == EAGAIN) continue;
        if (adChannel[chn].status == CHAisCLAIMED && cnt > 1) {
            adChannel[chn].status = CHAisREADY;
            break;
        }
        if (cnt > scnt+1) {  // Skip the echoed command
            int n=0;
            char *ptr=&rbuf[scnt+1];

            if (!strncmp(ptr, "ERROR", 5)) {
                printlog("ERROR: uk1104 rejected command %s", cmdFmt);
                break;
            }
            adChannel[chn].age = time(NULL);    // Refresh time stamp
            
            for (int i=0; i<strlen(ptr); i++) { // Save channel data
                if (ptr[i] == '\n' || ptr[i] == '\r') break;
                adChannel[chn].adBuffer[n++] = ptr[i];
            }
            break;
        }
    }
}

// uk1104 Command handler thread
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

            if (chn >= RELCHA) // realys
            {
                if (adChannel[chn].mode == ON)
                    sprintf(cmdFmt, RELxON, (chn-RELCHA)+1);
                else
                    sprintf(cmdFmt, RELxOFF, (chn-RELCHA)+1);
            } else {
                if (adChannel[chn].status == CHAisCLAIMED) {
                    sprintf(cmdFmt, CHxSETMOD, chn+1, adChannel[chn].type);
                } else {               
                    switch (adChannel[chn].type)
                    {
                        case AnaogIn:
                            sprintf(cmdFmt, CHxGETANALOG, chn+1);
                        break;
                        case TempIn:
                            sprintf(cmdFmt, CHxGETTEMP, chn+1);
                        break;
                        case DigIn:
                            sprintf(cmdFmt, CHxGET, chn+1);
                        break;
                        case DigOut:
                            sprintf(cmdFmt, adChannel[chn].mode == ON? CHxON : CHxOFF, chn+1);
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

    (void)tcgetattr(fd, &SerialPortSettings);     // Get the current attributes of the Serial port

    /* Setting the Baud rate */
    (void)cfsetispeed(&SerialPortSettings,B115200); // Set Read  Speed as 115200
    (void)cfsetospeed(&SerialPortSettings,B115200); // Set Write Speed as 115200

    /* 8N1 Mode */
    SerialPortSettings.c_cflag &= ~PARENB;  // Disables the Parity Enable bit(PARENB),So No Parity
    SerialPortSettings.c_cflag &= ~CSTOPB;  // CSTOPB = 2 Stop bits,here it is cleared so 1 Stop bit
    SerialPortSettings.c_cflag &= ~CSIZE;   // Clears the mask for setting the data size
    SerialPortSettings.c_cflag |=  CS8;     // Set the data bits = 8

    SerialPortSettings.c_cflag &= ~CRTSCTS;         // No Hardware flow Control
    SerialPortSettings.c_cflag |= CREAD | CLOCAL;   // Enable receiver,Ignore Modem Control lines


    SerialPortSettings.c_iflag &= ~(IXON | IXOFF | IXANY);  // Disable XON/XOFF flow control both i/p and o/p
    SerialPortSettings.c_lflag &= ~(ICANON | ISIG | ECHO | ECHOE);  // Non Cannonical mode
    
    SerialPortSettings.c_oflag &= ~OPOST;   // No Output Processing 

    // Setting Time outs
    SerialPortSettings.c_cc[VMIN] = 2;  // Read at least 2 characters 
    SerialPortSettings.c_cc[VTIME] = 0; // Wait 0 sec


    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) {  // Set the attributes to the termios structure
        printlog("UK1104: Error in setting serial attributes");
        return 1;
    } else
        printlog("UK1104: %s: BaudRate = 115200, StopBits = 1,  Parity = none", device);

    (void)tcflush(fd, TCIOFLUSH);  // Discards old data in the rx buffer

    (void)memset(adChannel, 0, sizeof(adChannel));
    pthread_attr_init(&attr);
    detachstate = PTHREAD_CREATE_DETACHED;
    (void)pthread_attr_setdetachstate(&attr, detachstate);
    (void)pthread_create(&t1, &attr, t_devMgm, NULL); // Start the command handler thread

    return 0;
}

/* API */
int adcInit(char *device, int a2dChannel)
{
    int fd = 0;

    if (a2dChannel > RELCHA) {
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
        if (portConfigure(fd, device)) {
            (void)close(fd);
            return 1;
        }
        serialDev.fd = fd;
        (void)fcntl(serialDev.fd, F_SETFL, FNDELAY);  // Non-blockning
    }

    adChannel[a2dChannel].type = a2dChannel >= TPMCH? TempIn : AnaogIn;
 
    adChannel[a2dChannel].status = CHAisCLAIMED;

    return 0;
}

/* API */
int adcRead(int a2dChannel)
{

    if (adChannel[a2dChannel].status != CHAisREADY)
        return 0;

    if (strlen(adChannel[a2dChannel].adBuffer) >1)
        adChannel[a2dChannel].curVal = atoi(adChannel[a2dChannel].adBuffer);


    if (adChannel[a2dChannel].age < time(NULL) - MAXAGE)
        adChannel[a2dChannel].curVal = 0;   // Zero out aged values

    return adChannel[a2dChannel].curVal;
}


/* API */
int relayStatus(void)
{
    int i, iter;
    int result = 0;

    for (i=1, iter=0; i<15; i<<=1, iter++)
    {
        if (adChannel[iter+RELCHA].status == CHAisREADY && adChannel[iter+RELCHA].mode == ON)
            result |= i;
    }
 
    return result;  // A bitmask
}


/* API */
void relaySet(int channels) // A bitmask
{
    int i, iter;

    for (i=1, iter=0; i<15; i<<=1, iter++)
    {
        if (adChannel[iter+RELCHA].status == CHAisNOTUSED)
            continue;

        if (channels & i) {
            adChannel[iter+RELCHA].mode = ON;
        } else {
            adChannel[iter+RELCHA].mode = OFF;
        }
    }
}

/* API */
void relayInit(int nchannels)
{
    int i, iter;
    int bm = 0;


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

    executeCommand(RELSGET, RELCHA);

    for(int i=0; i <nchannels; i++) {
        adChannel[i+RELCHA].status = CHAisCLAIMED;
        adChannel[i+RELCHA].type = Relay;
    }

    // Get the ASCII bitmap and set channel bits accordingly
    // Fmt: "1 0 0 1"
    for (i=1, iter=0; i<15; i<<=1, iter++) {
        if (adChannel[RELCHA].adBuffer[iter*2] == '1')
            bm |= i;
    }

    relaySet(bm);

    runThread = ON;
}

/* API */
int ioPinInit(int channel, int type) // Digin, Digout
{

    if (!serialDev.fd) {
        printlog("UK1104: IO Init: Device not initialized with adcInit()");
        return 1;
    }

    if (adChannel[channel].status == CHAisNOTUSED) {
        adChannel[channel].type = type;
        adChannel[channel].status = CHAisCLAIMED;
    } else {
        printlog("UK1104: IO Channel %d already claimed", channel);
        return 1;
    }
    return 0;      
}

/* API */
void ioPinset(int channel, int mode) // ON / OFF
{

    if (adChannel[channel].status != CHAisREADY)
        return;

    if (adChannel[channel].type != DigOut)
        return;

    adChannel[channel].mode = mode;
}

int ioPinGet(int channel)
{
     if (adChannel[channel].status != CHAisREADY)
        return OFF;

    return atoi(adChannel[channel].adBuffer); // 1=ON / 0=OFF
}

#endif // UK1104

/* API */
float tick2volt(int tick)
{
    float volt = 0.0;
    size_t len = (sizeof(voltSensor)/sizeof(int))/2;

    tick = ADCRESOLUTION-tick;   // Invert value
    if (tick > voltSensor[1][0]) return volt;
    // Compensate for nonlinearity in the sensor
    for (size_t i = 1; i < len-1; i++) {
        if (tick >= voltSensor[i][0]) {
            volt = (voltSensor[i][1] + voltSensor[i-1][1] + voltSensor[i+1][1])/3;
            volt +=  tick - voltSensor[i][0];
            volt *= ADCTICKSVOLT;
            break;
        }
    }
    
    return volt;
}

/* API */
float tick2current(int tick)
{
    float curr = 0.0;
    size_t len = (sizeof(currSensor)/sizeof(int))/2;

    tick = ADCRESOLUTION-tick;   // Invert value

    if (tick > currSensor[1][0]) return curr;

    // Compensate for nonlinearity in the sensor
    for (size_t i = 1; i < len-1; i++) {
        if (tick >= currSensor[i][0]) {
            curr = (currSensor[i][1] + currSensor[i-1][1] + currSensor[i+1][1])/3;
            curr +=  tick - currSensor[i][0];
            curr *= ADCTICKSCURR;
            break;
        }
    }
    
    return curr;
}

/* API */
void a2dNotice(int channel, float val, float low, float high)
{
    static float voltLow;
    static float VoltHigh;
    static char msg[100];
    struct stat sb;

    // Check the external script existent and mode
    if (stat(MSGPRG, &sb) || !(sb.st_mode & S_IXUSR))
        return;

    if (channel == voltChannel) {
        if (val == 0.0 || val > 16.0) return;

        // Low warning
        if (val <= low && voltLow == 0.0) {
            sprintf(msg, MSGVLOW, MSGPRG, val);
            ignored = system(msg);
            voltLow = val;
            VoltHigh = 0.0;
            return;
        }
        // Recover message
        if (val >= high && VoltHigh == 0.0) {
            sprintf(msg, MSGVHIGH, MSGPRG, val);
            ignored = system(msg);
            voltLow = 0.0;
            VoltHigh = val;
            return;
        }
        return;
    }
}

#ifdef MCP3208  // http://robsraspberrypi.blogspot.se/2016/01/raspberry-pi-adding-analogue-inputs.html
#ifndef UK1104
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
          printlog("MCP3208: Could not open SPI device %s", devspi);
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MODE, &spiDev.mode) < 0) {
          printlog("MCP3208: Could not set SPIMode (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MODE, &spiDev.mode) <0) {
          printlog("MCP3208: Could not set SPIMode (RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_BITS_PER_WORD, &spiDev.bitsPerWord) <0) {
          printlog("MCP3208ADC: Could not set SPI bitsPerWord (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_BITS_PER_WORD, &spiDev.bitsPerWord) < 0) {
          printlog("MCP3208: Could not set SPI bitsPerWord(RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &spiDev.speed) < 0) {
          printlog("MCP3208: Could not set SPI speed (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &spiDev.speed) <0) {
          printlog("MCP3208: Could not set SPI speed (RD)...ioctl fail");
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

/* API */
int adcInit(char *device, int a2dChannel)
{
    // check channel for adc
    if (a2dChannel > 7) {
        printlog("MCP3208: Channel must be less than 8 not  %d", a2dChannel);
        return 1;
    }

    if (!spiDev.status) {
        // Initialize ADC device
        if ( mcp3208SpiInit(device, SPI_MODE_0, 1000000, 8) ) {
            spiDev.status = 0;
            printlog("MCP3208: ADC Could not open device %s", device);
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
          spi[i].rx_buf        = (unsigned long)(data + i); // receive into "data"
          spi[i].len           = sizeof(*(data + i)) ;
          spi[i].delay_usecs   = 0 ;
          spi[i].speed_hz      = spiDev.speed ;
          spi[i].bits_per_word = spiDev.bitsPerWord ;
          spi[i].cs_change = 0;
     }

     return ioctl (spiDev.spiFd, SPI_IOC_MESSAGE(length), &spi);
}

/* API */
int adcRead(int a2dChannel)
{
    int a2dVal = 0;
    unsigned char data[3];
    
    if (!spiDev.status)
        return a2dVal;

    // Do A/D conversion
    data[0] = 0x06 | ((a2dChannel & 0x07) >> 7);
    data[1] = ((a2dChannel & 0x07) << 6);
    data[2] = 0x00;

    (void)spiWriteRead(data, sizeof(data));

    data[1] = 0x0F & data[1];
    a2dVal = (data[1] << 8) | data[2];

    return a2dVal;
}

/* API */
void relayInit(int nchannels)
{
}
int relayStatus(void)
{
    return 0;
}

void relaySet(int channels) // A bitmask
{
}
#endif // UK1104
#endif // MCP3208
#else
int relayStatus(void)
{
    return 0;
}

void relaySet(int channels) // A bitmask
{
}
#endif // DOADC

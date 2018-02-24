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

#define UK1104P         "\r"
#define PUK1104         "\r\n\r\n::"
#define CHxSETMOD       "CH%d.SETMODE(%d)\r"
#define CHxGETANALOG    "CH%d.GETANALOG\r"
#define CHxGETTEMP      "CH%d.GETTEMP\r"
#define CHxON           "CH%d.ON\r"
#define CHxOFF          "CH%d.OFF\r"
#define CHxGET          "CH%d.GET\r"
#define RELxON          "REL%d.ON\r"
#define RELxOFF         "REL%d.OFF\r"
#define RELSGET         "RELS.GET\r"
#define ON              1
#define OFF             0
#define IN              0
#define OUT             1

#define ADBZ    24
#define IOMAX   10      // 6+4 Channels
#define RELCHA  6       // Offset for realy status
#define IOWAIT  350000  // Wait for non blocked data .. usec and ..
#define MAXTRY  4       // ..before giving up omn ser. data.
#define MAXAGE  20      // Invalidate after .. s

#define CHAisNOTUSED    0
#define CHAisCLAIMED    1
#define CHAisREADY      2

static struct serial {
    int fd;
} serialDev;

enum types {
    DigIn = 1,
    DigOut,
    AnaogIn,
    TempIn,
    Relay
};

struct adData
{
    char adBuffer[ADBZ];
    float curVal;
    int mode;
    int type;
    int status;
    time_t age;
};

static struct adData adChannel[IOMAX];

static int runThread = ON;


// Array 8-16v ~49 mv step
// Linear compesation for optocoupler H11F3 DIP-6 connected to A/D pin.
// R Diod = 5.6K
// R FET Pull-up = 8.2K
static int osensor [][2] =
{
    {328,	328},
    {326,	327},
    {324,	326}, // 16.0
    {322,	325},
    {320,	324},
    {318,	322},
    {316,	320},
    {314,	318},
    {312,	316}, // 15.5
    {310,	315},
    {308,	314},
    {306,	312},
    {304,	310},
    {302,	308},
    {300,	306},
    {298,	304},
    {296,	302},
    {294,	300},
    {292,	298},
    {290,	296},
    {288,	295},
    {286,	294}, // 14.4
    {284,	293},
    {282,	291},
    {280,	290},
    {278,	289},
    {276,	288},
    {274,	286},
    {272,	284},
    {270,	282},
    {268,	280},
    {266,	278},
    {264,	276},
    {262,	274}, // 13.4
    {260,	273},
    {258,	271},
    {256,	268},
    {254,	266},
    {252,	264},
    {250,	263},
    {248,	262},
    {246,	260},
    {244,	258},
    {242,	255},
    {240,	253},
    {238,	252},
    {236,	251},
    {234,	252}, // 12.3
    {232,	250},
    {230,	248},
    {228,	246},
    {226,	244},
    {224,	242},
    {222,	240},
    {220,	238},
    {218,	236},
    {216,	234},
    {214,	233},
    {212,	232}, // 11.4
    {210,	230}, 
    {208,	229},
    {206,	228},
    {204,	226},
    {202,	224},
    {200,	222},
    {198,	220},
    {196,	218},
    {194,	216},
    {192,	214},
    {190,	213}, 
    {188,	212}, // 10.4
    {186,	211},
    {184,	210},
    {182,	208},
    {180,	206},
    {178,	204}, // 9.88
    {176,	202},
    {174,	200},
    {172,	198},
    {170,	196},
    {168,	194},
    {166,	192},
    {164,	190},
    {162,	188},
    {160,	186},
    {158,	184},
    {156,	182},
    {154,	180}, 
    {152,	178}, // 8,75
    {150,	176},
    {148,	174},
    {146,	173},
    {144,	172},
    {142,	170}, 
    {140,	169}, // 8.30
    {138,	168},
    {136,	168},
    {134,	166},
    {132,	165},
    {130,	164}
};

static int getPrompt(void)
{
    char buffer[ADBZ];
    size_t cnt;
    int rval = 1;
    extern int errno;

    if (!serialDev.fd) return 1;

    for (int i = 0; i < 3; i++) 
    {
        (void)tcflush(serialDev.fd, TCIOFLUSH);

        (void)sprintf(buffer, UK1104P);
        cnt = write(serialDev.fd, buffer, strlen(buffer)); // Write the UK1104 init wake up string
        if (cnt != strlen(buffer)) {
            printf("UK1104: Error in sending get prompt string");
            return rval;
        }

        (void)memset(buffer, 0 , ADBZ);
        for (int try = 0; try < MAXTRY; try++)
        {
            (void)usleep(IOWAIT);
            cnt = read(serialDev.fd, buffer, ADBZ); // Get the response
            if (cnt == -1 && errno == EAGAIN) continue;
            if (cnt > 1 && !strncmp(buffer, PUK1104, 6)) {
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

    } else if (adChannel[chn].type != DigOut) {

        (void)memset(rbuf, 0 , ADBZ);

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
                char *ptr;
                int n=0;
                adChannel[chn].age = time(NULL);
                ptr=&rbuf[scnt+1];
                for (int i=0; i<strlen(ptr); i++) {
                    if (ptr[i] == '\n' || ptr[i] == '\r') break;
                    adChannel[chn].adBuffer[n++] = ptr[i];
                }
            }
        }
    }
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

    if (serialDev.fd) return 0;

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
    (void)pthread_create(&t1, &attr, t_devMgm, NULL); // Start the reader thread

    return 0;
}

/* API */
int adcInit(char *device, int a2dChannel)
{
    int fd;

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
    }

    if (portConfigure(fd, device)) {
        (void)close(fd);
        serialDev.fd = 0;
        return 1;
    }
    serialDev.fd = fd;

   (void)fcntl(serialDev.fd, F_SETFL, FNDELAY);  // Non-blockning

    adChannel[a2dChannel].type = a2dChannel >= TPMCH? TempIn : AnaogIn;
 
    adChannel[a2dChannel].status = CHAisCLAIMED;

    return 0;
}

/* API */
float adcRead(int a2dChannel)
{

    if (adChannel[a2dChannel].status != CHAisREADY) {

        return 0;
    }

    if (strlen(adChannel[a2dChannel].adBuffer)) {
        adChannel[a2dChannel].curVal = atof(adChannel[a2dChannel].adBuffer);
    }

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
    int result = 0;


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

    // Get the bitmap and set accordingly
    for (i=1, iter=0; i<15; i<<=1, iter++) {
        if (adChannel[RELCHA].adBuffer[iter*2] == '1')
            result |= i;
    }

    relaySet(result);

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

    if (adChannel[channel].type != DigIn)
        return OFF;

    return atoi(adChannel[channel].adBuffer); // 1=ON / 0=OFF
}

/* API */
float tick2volt(int tick)
{
    float volt = 0.0;
    size_t len = (sizeof(osensor)/sizeof(int))/2;

    tick = 1023-tick;   // Invert value

    if (tick > osensor[1][0]) return volt;

    // Compensate for nonlinearity in the sensor
    for (size_t i = 1; i < len-1; i++) {
        if (tick >= osensor[i][0]) {
            volt = (osensor[i][1] + osensor[i-1][1] + osensor[i+1][1])/3;
            volt +=  tick - osensor[i][0];
            volt *= 0.0495;
            break;
        }
    }
    
    return volt;
}


#endif // UK1104

/* API */
void a2dNotice(int channel, float val, float low, float high)
{
    static float voltLow;
    static float VoltHigh;
    static char msg[100];
    struct stat sb;

    if (stat(MSGPRG, &sb) || !(sb.st_mode & S_IXUSR))
        return;

    if (channel == voltChannel) {
        if (val == 0.0 || val > 16.0) return;

        if (val <= low && voltLow == 0.0) {
            sprintf(msg, MSGVLOW, MSGPRG, val);
            system(msg);
            voltLow = val;
            VoltHigh = 0.0;
            return;
        }
        if (val >= high && VoltHigh == 0.0) {
            sprintf(msg, MSGVHIGH, MSGPRG, val);
            system(msg);
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




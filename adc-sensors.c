/*
 * adc-sensors.c
 *
 *  Copyright (C) 2013-2024 by Erland Hedman <erland@hedmanshome.se>
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
#include <time.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <termios.h> 
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <netdb.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "wsocknmea.h"

static int ignored __attribute__((unused));

#ifdef SMARTPLUGDEV
#define SMARTPLUGERROR      "/tmp/smartplug-error.stat"

#define SMARTPLUGPOLLCMD    "hs100poll.sh %s %i %s"
#define SMARTPLUGONCMD      "hs100 %s on > /dev/null 2>&1"
#define SMARTPLUGOFFCMD     "hs100 %s off > /dev/null 2>&1"
#define SMARTPLUGHOSTNAME   "HS100-%d"

static int smartplugLock = 0;
static int smartplugError = 0;

// Manage smartplugs on and off
void smartplugSet(int status)
{
    char cmd[100];
    char hsdev[40];
    struct hostent *hp;

    int i, iter, ret;
    int fd;
    struct stat sb;

     if (!stat(SMARTPLUGERROR, &sb)) {
        return;
    }

    for (i=1, iter=1; i<3; i<<=1, iter++)
    {
        sprintf(hsdev, SMARTPLUGHOSTNAME, iter);
        if ((hp = gethostbyname(hsdev)) == NULL) continue;

        if (status & i) {
            (void)sprintf(cmd, SMARTPLUGONCMD, inet_ntoa( *( struct in_addr*)( hp -> h_addr_list[0])));
        } else {
            (void)sprintf(cmd, SMARTPLUGOFFCMD, inet_ntoa( *( struct in_addr*)( hp -> h_addr_list[0])));
        }

        if (fork() == 0)
        {
            signal(SIGCHLD, SIG_DFL);
            signal(SIGHUP, SIG_DFL);

            ret = system(cmd);

            if (WEXITSTATUS(ret) != 0) {
                printlog("Failed to execute '%s'", cmd);          
                if ((fd=open (SMARTPLUGERROR, O_RDWR|O_CREAT|O_APPEND)) >0) {
                    sprintf(cmd, "%d\n", iter);
                    write(fd, cmd, strlen(cmd));
                    close(fd);
                }  
            }
            exit(0);
        }
    }
}

// Get status smartplug device(s)
unsigned int smartplugGet(void)
{
    char buf[100];
    char status[20];
    static unsigned int rval;
    static unsigned int curStat;
    int bit = 0;
    FILE *fd;

    if ((fd = fopen(SMARTPLUGERROR, "r")) != NULL) {
        rval = 0;
         while ((fgets(buf, sizeof(buf), fd)) != NULL) {
            rval |= 1 << (atoi(buf)-1); 
        }
        rval |= 1 << 5;     // Error bit
        fclose(fd);
        unlink(SMARTPLUGERROR);
        smartplugError = 2;
        return rval;
    }

    if (smartplugError > 0) {
        smartplugError--;
        return rval;
    }

    if (smartplugLock) {
        return curStat;
    }

    rval = 0;

    if ((fd = fopen(SMARTPLUGSTS, "r")) != NULL) {
        while ((fgets(buf, sizeof(buf), fd)) != NULL) {

            (void)sscanf(buf, "%d %s", &bit, status);

            if (strncmp("1", status, 2) == 0) {
                rval |= 1 << (bit-1);
            }
        }

        fclose(fd);
        curStat = rval;
    }

    return rval;
}

// Poll status smartplug devices
void *t_smartplug(void *arg)
{
    char cmd[200];
    char hsdev[40];
    static int rval = 0;
    int *doRun = arg;
    struct hostent *hp;

    printlog("Starting smartplug services");

    while(*doRun)
    {
        for (int i = 1; i < 3; i++)
        {
            smartplugLock = 1;
            if (i == 1) unlink(SMARTPLUGSTS);

            sprintf(hsdev, SMARTPLUGHOSTNAME, i);
            if ((hp = gethostbyname(hsdev)) == NULL) continue;

            sprintf(cmd, SMARTPLUGPOLLCMD, inet_ntoa( *( struct in_addr*)( hp -> h_addr_list[0])), i, SMARTPLUGSTS);
            (void)system(cmd);
        }
        smartplugLock = 0;
        sleep(2);
    }

    printlog("Stopping smartplug services");
    pthread_exit(&rval);
}

#else

void smartplugSet(int status)
{
    return;
}

unsigned int smartplugGet(void)
{
    return -1;
}

void *t_smartplug(void *arg)
{
    static int rval = 0;
    pthread_exit(&rval);
}

#endif 

#ifdef DOADC

#ifdef UK1104   // https://www.canakit.com/ 4-Channel USB Relay Board with 6-Channel A/D Interface

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
static time_t relayTimeout[4];
static time_t relayTimeoutSec[4];

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
                printlog("UK1104: uk1104 rejected command %s", cmdFmt);
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
        relaySchedule();
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
        if (adChannel[iter+RELCHA].status == CHAisREADY && adChannel[iter+RELCHA].mode == ON) {
            result |= i;
            if (relayTimeout[iter] > 0 && time(NULL) >= relayTimeout[iter]) {
                printlog("UK1104: Relay-%d expired", iter+1);
                adChannel[iter+RELCHA].mode = OFF;
                relayTimeoutSec[iter] = relayTimeout[iter] = 0;
            }  else {
                relayTimeoutSec[iter] = relayTimeout[iter] - time(NULL);
            }
        }
    }

    if (result < 0 || result > 15) {
        printlog("UK1104: Corrupted channel bitmask for relayStatus(%d)", result);
        return 0;
    }

    return result;  // A bitmask
}

char *relayTimeouts(void)
{
    static char buf[100];
    sprintf(buf, "'relayTm1':'%ld','relayTm2':'%ld','relayTm3':'%ld','relayTm4':'%ld'", relayTimeoutSec[0], relayTimeoutSec[1], relayTimeoutSec[2] ,relayTimeoutSec[3]);
    return buf;
}

/* API */
void relaySchedule(void)
{
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail;
    char cmd[60];
    int rval;
    int rtime = 0;
    int day = 0;
    int tmo = 0;

    (void)sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READONLY, 0);
    if (conn == NULL) {
        printlog("UK1104: Failed to open database %s to handle relay timeouts: ", (char*)sqlite3_errmsg(conn));
        return;
    }

    for (int rel = 1; rel <= 4; rel++) {

        if (adChannel[(rel-1)+RELCHA].status == CHAisNOTUSED)
        continue;

        sprintf(cmd, "select time from devrelay where id=%d;", rel);
        rval = sqlite3_prepare_v2(conn, cmd, -1, &res, &tail);
        if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            rtime = sqlite3_column_int(res, 0);
            sqlite3_finalize(res);
        }

        if (rtime > 0) {
            sprintf(cmd, "select days from devrelay where id=%d;", rel);
            rval = sqlite3_prepare_v2(conn, cmd, -1, &res, &tail);
            if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                day = sqlite3_column_int(res, 0);
                sqlite3_finalize(res);
            }
            if (day > 0) {
                sprintf(cmd, "select timeout from devrelay where id=%d;", rel);
                rval = sqlite3_prepare_v2(conn, cmd, -1, &res, &tail);
                if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                    tmo = sqlite3_column_int(res, 0);
                    sqlite3_finalize(res);
                }
                if (tmo > 0) {
                    time_t t = time(NULL);
                    struct tm *now = localtime(&t);
                    int wday = now->tm_wday == 0? 6 : now->tm_wday-1;
                    int ts = now->tm_min + now->tm_hour*60;

                   if (day & (1 << wday) && ts >= rtime && rtime+tmo > ts && adChannel[(rel-1)+RELCHA].mode != ON) {
                    //if (day & (1 << wday)) {
                        adChannel[(rel-1)+RELCHA].mode = ON;
                        printlog("UK1104: Scheduled duration of %d minutes set on Relay-%d", tmo, rel);
                        relayTimeout[rel-1] = time(NULL)+tmo*60;
                    }
                }
            }
        }
    }

    (void)sqlite3_close(conn);

    relayStatus();
}

/* API */
void relaySet(int channels, char *tmos) // A bitmask and time-out values
{
    int i, iter, tmcnt=0;
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail;
    char cmd[60];
    char *args[20];
    int rval;
    int tmo = 0;

    if (channels < 0 || channels > 15) {
        printlog("UK1104: Corrupted channel bitmask for relaySet(%d)", channels);
        return;
    }

   (void)sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, 0);
    if (conn == NULL) {
        printlog("UK1104: Failed to open database to handle relay timeouts: %s", (char*)sqlite3_errmsg(conn));
        return;
    }

    if (tmos !=NULL && strlen(tmos)) {
        args[tmcnt] = strtok(tmos, "|");
        while(args[tmcnt] != NULL) {
            args[++tmcnt] = strtok(NULL, "|");
        }
    }

    tmcnt = tmcnt == 4? tmcnt : 0;
    
    for(int row=0; row < tmcnt; row++) {
        if (atoi(args[row]) > 0) {
            sprintf(cmd, "update devrelay set timeout=%s where id=%d", args[row], row+1);
            if (sqlite3_prepare_v2(conn, cmd, -1, &res, &tail) == SQLITE_OK) {
                 if (sqlite3_step(res) != SQLITE_DONE) {
                    printlog("UK1104: Failed to update relay timeout data: %s", (char*)sqlite3_errmsg(conn));
                }
                sqlite3_finalize(res);
            } else {
                printlog("UK1104: Failed to update relay timeout data: %s", (char*)sqlite3_errmsg(conn));
            }         
        }
    }

    for (i=1, iter=0; i<15; i<<=1, iter++)
    {
        if (adChannel[iter+RELCHA].status == CHAisNOTUSED)
            continue;

        if (channels & i) {
            
            sprintf(cmd, "select timeout from devrelay where id=%d;", iter+1);
            rval = sqlite3_prepare_v2(conn, cmd, -1, &res, &tail);        
            if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                    tmo = sqlite3_column_int(res, 0);
                    sqlite3_finalize(res); 
            } else {
                printlog("UK1104: Timeout of %d minutes FAILED on Relay-%d", tmo, iter+1);
                tmo = relayTimeout[iter] = 0;
            }

            if (tmo > 0 && relayTimeout[iter] < time(NULL)) {
                printlog("UK1104: Timeout of %d minutes set on Relay-%d", tmo, iter+1);
                relayTimeout[iter] = time(NULL)+tmo*60;
                adChannel[iter+RELCHA].mode = ON;
            }

        } else {
            adChannel[iter+RELCHA].mode = OFF;
        }
    }

    (void)sqlite3_close(conn);
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

    relaySet(bm, "");

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
float tick2volt(int tick, float tickVolt, int invert)
{
    float volt = 0.0;

    if (invert)
        tick = ADCRESOLUTION-tick;   // Invert value

    if (tickVolt == 0.0)
        tickVolt = ADCTICKSVOLT;
        
    volt = tick * tickVolt;
    //printf("%.2f, %d   \n", volt, tick ); fflush(stdout);
    
    return volt;
}

/* API */
float tick2current(int tick, float tickVolt, float crefVal, float crShunt, float gain, int invert)
{
    float svolt = 0.0;
    float curr = 0.0;

    if (tickVolt == 0.0)
        tickVolt = ADCTICKSCURR;

    svolt = tick * tickVolt;

    if (svolt > 0) {

        svolt -= crefVal;

        svolt /= gain;
         
        curr =  (svolt / crShunt);

        if (invert) {
            curr = curr > 0? 0 - fabs(curr) : fabs(curr);
        }
    }

    // printf("curr=%.2f, svolt=%.6f, ref=%.2f, shunt=%.3f, gain=%.1f, ticks=%d     \r", curr, svolt, crefVal, crShunt, gain, tick ); fflush(stdout);

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

void relaySet(int channels, char *tmos) // A bitmask and time-out values
{
}

#endif // UK1104
#endif // MCP3208
#else
int relayStatus(void)
{
    return 0;
}

void relaySet(int channels, char *tmos) // A bitmask and time-out values
{
}

char *relayTimeouts(void)
{
    static char buf[100];
    sprintf(buf, "'relayTm1':'0','relayTm2':'0','relayTm3':'0','relayTm4':'0'");
    return buf;
}

#endif // DOADC

/*
 * wsocknmea.c
 *
 *  Copyright (C) 2013-2019 by Erland Hedman <erland@hedmanshome.se>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Description:
 * Connect to an nmea mux such as the kplex daemon and collect data
 * that virtual instruments can obtain by using the websocket transport
 * protocol by issuing requests that this server will reply with
 * JSON formatted data to the requester.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <zlib.h>
#include <ctype.h>
#include <pthread.h>
#include <sqlite3.h>
#include <sys/mman.h>
#include <libwebsockets.h>
#include <syslog.h>
#include <math.h>
#include <portable.h>
#include <nmea.h>
#include <sixbit.h>
#include <vdm_parse.h>
#include "wsocknmea.h"


// Configuration
#define NMEAPORT 10110          // Port 10110 is designated by IANA for "NMEA-0183 Navigational Data"
#define NMEAADDR "127.0.0.1"    // localhost for TCP for UDP use 239.194.4.4 for a multicast address
#define WSPORT 443              // Port for the websocket protocol (to be allowed by firewall)
#ifndef KPCONFPATH
#define KPCONFPATH  "/etc/default/kplex.conf"   // KPlex configuration file writable for webserver
#endif
#ifndef UPLOADPATH
#define UPLOADPATH  "/var/www/upload"           // PATH to NMEA recordings
#endif

#define FIFOKPLEX   "/tmp/fifo_kplex"       // KPlex fifo for file nmea input
#define FIFOPNMEA   "/tmp/fifo_nmea_p"      // KPlex fifo for "$P". These extended messages are not standardized. 

#define WSREBOOT    "/tmp/wss-reboot"           // Arguments to this server from WEB GUI

#ifndef UID         // UID/GID - Set to webserver's properties so that db and kplex config files can be writtin to
#define UID 33
#endif
#ifndef GID
#define GID 33
#endif

#define DEFAULT_AUTH "200ceb26807d6bf99fd6f4f0d1ca54d4" // MD5 password = "administrator" used outside LAN
#define DEFAULT_AUTH_DURATION   60                      // Seconds of open GUI

#ifdef REV
#define SWREV REV
#else
#define SWREV __DATE__
#endif

// Sentences to control AIS transmitter on/off. Works for Ray, SRT and others.
// NOTE: "--QuaRk--" is actually a password that might changed by the manufacturer.
#define AIXTRXON	"\r\n$PSRT,012,,,(--QuaRk--)*4B\r\n$PSRT,TRG,02,00*6A\r\n"
#define AISTRXOFF	"\r\n$PSRT,012,,,(--QuaRk--)*4B\r\n$PSRT,TRG,02,33*6A\r\n"

#define MAX_LWSZ    32768   // Max payload size for uncompressed websockets data 
#define WS_FRAMEZ   8192    // Websocket frame size  
#define MAX_TTYS    50      // No of serial devices to manage in the db
#define MAX_NICS    6       // No of nics to manage in the db

#define POLLRATE    5       // Rate to collect data in ms.

#define INVALID     4       // Invalidate current sentences after # seconds without a refresh from talker.

#define NMPARSE(str, nsent) !strncmp(nsent, &str[3], strlen(nsent))

// Request codes from virtual instruments
enum requests {
    SpeedOverGround     = 100,
    SpeedThroughWater   = 101,
    DepthAndTemp        = 110,
    CompassHeading      = 120,
    GPS                 = 121,
    WindSpeedAndAngle   = 130,
    GoogleMapFeed       = 140,
    GoogleAisFeed       = 141,
    GoogleAisBuddy      = 142,
    SensorVolt          = 200,
    SensorCurr          = 201,
    SensorTemp          = 202,
    SensorRelay         = 203,
    tdtStatus           = 205,
    AisTrxStatus        = 206,
    WaterMakerData      = 210,
    ServerPing          = 900,
    TimeOfDAy           = 901,
    SaveNMEAstream      = 904,
    StatusReport        = 906,
    Authentication      = 908,
    UpdateSettings      = 910
};

static int debug = 0;
static int backGround = 0;
static int muxFd = 0;
static pthread_t threadKplex = 0;
static pid_t pidKplex = 0;
static int kplexStatus = 0;
static int pNmeaStatus = 1;
static int sigExit = 0;
static int smartplugRun = 1;
static useconds_t lineRate = 1;
static int fileFeed = 0;
static struct sockaddr_in peer_sa;
static struct lws_context *ws_context = NULL;
static int socketType = 0;
static char interFace[40];
static int socketCast = 0;
static char *programName;
static int unusedInt __attribute__((unused));   // To make -Wall shut up
static FILE * unusedFd __attribute__((unused));

#ifdef MT1800
#define WMMULTIADDR "224.0.0.1" // Join control unit
#define WMPORT 49152
static int wmsockFd;
static struct sockaddr_in wmpeer_sa;
#endif

typedef struct {
    // Static configs from GUI sql db
    int     map_zoom;           // Google Map Zoom factor;
    int     map_updt;           // Update Time
    int     depth_vwrn;         // Depth visual low warning
    float   depth_transp;       // Depth of transponer
    char    adc_dev[40];        // ADC in /dev
    char    ais_dev[40];        // AIS in /dev
    char    fdn_outf[120];      // NMEA stream output file name
    FILE    *fdn_stream;        // Save NMEA stream file
    time_t  fdn_starttime;      // Start time for recording
    time_t  fdn_endtime;        // End time for recording
    char    fdn_inf[PATH_MAX];  // NMEA stream input file
    char    md5pw[40];          // Password for GUI
    int     m55pw_ts;           // Passwd valid x seconds;
    char    client_ip[20];      // Granted client
} in_configs;

static in_configs iconf;

typedef struct {
    long    my_userid;          // AIS own user i.d
    long    my_buddy;           // AIS new buddy
    char    my_name[80];        // AIS own name
    int     my_useais;          // AIS use or not
} in_aisconfigs;

static in_aisconfigs aisconf;

typedef struct {
    // Dynamic data from sensors
    float   rmc;        // RMC (Speed Over Ground) in knots
    time_t  rmc_ts;     // RMC Timestamp
    float   stw;        // Speed of vessel relative to the water (Knots)
    time_t  stw_ts;     // STW Timestamp
    float   dbt;        // Depth in meters
    time_t  dbt_ts;     // DBT Timestamp
    float   mtw;        // Water temperature
    time_t  mtw_ts;     // Water temperature Timestamp
    float   hdm;        // Heading
    time_t  hdm_ts;     // HDM Timestamp
    float   vwra;       // Relative wind angle (0-180)
    float   vwta;       // True wind angle
    time_t  vwr_ts;     // Wind data Timestamp
    time_t  vwt_ts;     // True wind data Timestamp
    int     vwrd;       // Right or Left Heading
    float   vwrs;       // Relative wind speed knots
    float   vwts;       // True wind speed
    char    gll[40];    // Position Latitude
    time_t  gll_ts;     // Position Timestamp
    char    glo[40];    // Position Longitude
    char    glns[2];    // North (N) or South (S)
    char    glne[2];    // East (E) or West (W)
    int     txs;        // AIS transponder transmitter status
    time_t  txs_ts;     // AIS transponder Timestamp
    // Sensors non NMEA
    float   volt;       // Sensor Volt
    time_t  volt_ts;    // Volt Timestamp
    float   curr;       // Sensor Current
    time_t  curr_ts;    // Current Timestamp
    float   temp;       // Sensor Temp
    time_t  temp_ts;    // Temp Timestamp
    float   kWhp;       // Kilowatt hour - charged
    float   kWhn;       // Kilowatt hour - consumed
    time_t  upTime;     // Server's uptime
    time_t  startTime;  // Server's starttime
#ifdef MT1800
    float   cond;       // Condictivity from Watermaker
    time_t  cond_ts;    // Conductivity Timestamp
    float   ctemp;      // Water temperature from Watermaker
    time_t  ctemp_ts;   // Temperature Timestamp
    int     cwqvalue;   // Water quality
    int     cflowh;     // Flow / Hour
    int     cflowt;     // Flow tot
    int     cflowp;     // Preset volume
    int     crunsts;    // MT-1800 run state
#endif
} collected_nmea;

static collected_nmea cnmea;

void printlog(char *format, ...)
{
    va_list args;
    char buf[200];
    static char oldbuf[200];
    int ern = errno;

    va_start (args, format);         // Initialize the argument list.

    vsprintf(buf, format, args); 

    if (!strncmp(buf, oldbuf, sizeof(buf))) {
        va_end(args);
        return; // Do not repeate same msgs from thread loops
    } else {
        strncpy(oldbuf,buf, sizeof(oldbuf));
    }

    if (backGround) {
        syslog(LOG_NOTICE, "%s", buf);
    } else {
        fprintf(stderr, "%s\n", buf);
    }

    va_end(args);
    errno = ern;
}

static void do_sensors(time_t ts, collected_nmea *cn)
{
#ifdef MT1800
    int cnt;
    char msgbuf[100];
    socklen_t wmsl = sizeof(wmpeer_sa);
#endif

#ifdef DOADC

    float a2dVal;
    float crefVal;
    int ad2Tick;
    static int firstTurn;
    static int ccnt;
    static float avcurr;
    static float sampcurr[200];  // No of samples to collect
    static int tcnt;
    static float avtemp;
    static float samptemp[20];
    static int vcnt;
    static float sampvolt[20];
    static float avvolt;
    const float tickVolt = 0.01356;     // Volt / tick according to external electrical circuits
    const float tickcrVolt = 0.004882;   // 10-bit adc over 5V for current messurement
    const float crShunt = 0.00025;        // Current shunt (ohm) according to external electrical circuits
    const float cGain = 50;             // Current sense amplifier gain for LT1999-50
    const float cZero = 0.1;            // Sense lines in short-circuit should read 0
    const int linearize = 0;            // No extra compesation needed

    ad2Tick = adcRead(voltChannel);
    a2dVal = tick2volt(ad2Tick, tickVolt, 0); // Return voltage, no invert

    // Calculate an average in case of ADC drifts.
    if (a2dVal >= VOLTLOWLEVEL) {
        if (linearize) {
            sampvolt[vcnt++] = a2dVal;
            if (vcnt >= sizeof(sampvolt)/sizeof(float)) {
                vcnt = 0;
                for (int i=0; i < sizeof(sampvolt)/sizeof(float); i++) {
                    avvolt += sampvolt[i];
                }
                avvolt /= sizeof(sampvolt)/sizeof(float);
            }
        } else {
            avvolt = a2dVal;
        }
        cn->volt = avvolt;
        cn->volt_ts = ts;
    }

    // Alert about low voltage/ENV
    if (! firstTurn ) {
        a2dNotice(voltChannel, cn->volt, 11.5, 12.5);
        firstTurn = 1;
    }

    //ad2Tick = adcRead(crefChannel); // Read volt refrerence from current sensor
    //crefVal = tick2volt(ad2Tick, tickcrVolt, 0); // Return voltage, no invert
    crefVal = 2.486;    // Reference voltage measured by hand

    ad2Tick = adcRead(currChannel);
    a2dVal = tick2current(ad2Tick, tickcrVolt, crefVal, crShunt, cGain, 0)-cZero;   // Return current, no invert

    // Calculate an average in case of ADC drifts.
    if (a2dVal >= CURRLOWLEVEL) {
        if (linearize) {
            int i;
            sampcurr[ccnt++] = a2dVal;
            if (ccnt >= sizeof(sampcurr)/sizeof(float)) {
                ccnt =  0;
                for (i=0; i < sizeof(sampcurr)/sizeof(float); i++) {
                    avcurr += sampcurr[i];
                }
                avcurr /= sizeof(sampcurr)/sizeof(float);
            }
        } else {
            avcurr = a2dVal;
        }
        cn->curr = avcurr;
        cn->curr_ts = ts;
    }

    a2dVal = adcRead(tempChannel);
    a2dVal *= ADCTICKSTEMP;
    // Calculate an average in case of ADC drifts.
    if (a2dVal >= TEMPLOWLEVEL) {
        if (linearize) {
            samptemp[tcnt++] = a2dVal;
            if (tcnt >= sizeof(samptemp)/sizeof(float)) {
                tcnt = 0;
                for (int i=0; i < sizeof(samptemp)/sizeof(float); i++) {
                    avtemp += samptemp[i];
                }
                avtemp /= sizeof(samptemp)/sizeof(float);
            }
        } else {
            avtemp = a2dVal;
        }
        cn->temp = avtemp;
        cn->temp_ts = ts;
    }

#else
    // Just for demo
    cn->volt = (float)13.0 + (float)(rand() % 18)/100;
    cn->volt_ts = ts;
    cn->curr = (float)-2.0 + (float)(rand() % 18)/100;
    cn->curr_ts = ts;
    cn->temp = (float)20.0 + (float)(rand() % 18)/100;
    cn->temp_ts = ts;
#endif

#ifdef MT1800
    // Get data from the watermaker's external control box
    memset(msgbuf, 0, sizeof(msgbuf));
    if ((cnt=recvfrom(wmsockFd, msgbuf, sizeof(msgbuf), 0, (struct sockaddr *) &wmpeer_sa, &wmsl)) >0) {
        //printf( "Got '%s' as multicasted message. Lenght = %d\n", msgbuf, cnt );
        sscanf(msgbuf, "%f %f %d %d %d %d %d", &cn->cond, &cn->ctemp, &cn->cwqvalue, &cn->cflowh, &cn->cflowt, &cn->cflowp, &cn->crunsts);
        cn->cond_ts = cn->ctemp_ts =  ts;
    }
#endif
}

static void parse(char *line, char **argv)
{
    *argv++ = programName;

     while (*line != '\0') { 
          while (*line == ' ' || *line == '\t' || *line == '\n')
               *line++ = '\0';     // replace white spaces with 0
          *argv++ = line;          // save the argument position 
          while (*line != '\0' && *line != ' ' && 
                 *line != '\t' && *line != '\n') 
               line++;             // skip the argument until ...   
     }
     *argv = '\0';                 // mark the end of argument list
}

static void exit_clean(int sig)
{
    
    printlog("Terminating NMEA and KPLEX Services - reason: %d", sig);  
      
    fileFeed = 0;
    sigExit = 1;
    int fd;
    pid_t pid;
    char argstr[100];
    
    pNmeaStatus = 0;
    smartplugRun = 0;

    sleep(1);
 
#if 0
    if (threadKplex) {
        if (pthread_self() != threadKplex)
            pthread_cancel(threadKplex);
    }
#endif
    
    if (pidKplex) {
        kill (pidKplex, SIGINT);
        pidKplex = 0;
    }

    sleep(2);
    
    if(muxFd > 0)
       close(muxFd);

#ifdef MT1800
    if (wmsockFd >0)
        close(wmsockFd);
#endif
  
    if (ws_context != NULL) 
        lws_context_destroy(ws_context);  

    if (sig == SIGSTOP) {
        if ((fd = open(WSREBOOT, O_RDONLY)) >0) {
            if (read(fd, argstr, sizeof(argstr))>0) {
                (void)close(fd); (void)unlink(WSREBOOT);

                pid = fork();
        
                if (pid == 0) {  
                    /* child */
                    char *args[100];
                    sleep(2);
                    parse(argstr, args);
                    execvp(programName, args);
                    printlog("Failed to execute  %s: %s", programName, strerror(errno));
                    exit(errno);
                }
            }
        }
    }
    (void)unlink(WSREBOOT);
    (void)unlink(SMARTPLUGSTS);

    if (backGround) {
        closelog();
    }

    exit(EXIT_SUCCESS);
}

static char *getf(int pos, char *str)
{ // Extract an item from a sentence

    int len=strlen(str);
    int i,j,k;
    int npos=0;
    static char out[200];

    memset(out,0,sizeof(out));
    pos--;

    for (i=0; i<len;i++) {
        if (str[i]==',') {
            if (npos++ == pos) {
                strcat(out, &str[++i]);
                j=strlen(out);
                for (k=0; k<j; k++) {
                    if (out[k] == ',') {
                        out[k] = '\0';
                        i=len;
                        break;
                    }
                }
            }
        }
    }

    return (out);
}

// Open a socket to the kplex MUX
static int nmea_sock_open(int kplex_fork)
{
    u_int yes = 1;
    struct ip_mreq mreq;
    long flags;

    if (muxFd > 0)
        close(muxFd);
    
    // Create the socket
    if (!socketType) socketType = SOCK_STREAM;
    if (ntohs(peer_sa.sin_port) == 0) peer_sa.sin_port = htons(NMEAPORT);
    if (peer_sa.sin_addr.s_addr == 0) peer_sa.sin_addr.s_addr = inet_addr(NMEAADDR);

    if ((muxFd = socket(AF_INET, socketType, 0)) < 0) {
        printlog("Failed to create NMEA socket: %s", strerror(errno));
        return 1;
    }

    // Allow multiple sockets to use the same port number
    if (setsockopt(muxFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        printlog("Failed to set REUSEADDR on NMEA socket: %s", strerror(errno));
        close(muxFd);
        return 1;
    }

    if (socketType == SOCK_DGRAM) { // UDP part needs more testing

        if (strlen(interFace)) {
            if (setsockopt(muxFd, SOL_SOCKET, SO_BINDTODEVICE, interFace, strlen(interFace))) {
                printlog("Failed to bind NMEA socket to device %s: %s", interFace, strerror(errno));
                close(muxFd);
                return 1;
            }
        }

        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(peer_sa.sin_addr), str, INET_ADDRSTRLEN); 
    
        if (socketCast == SO_BROADCAST) {
            //peer_sa.sin_addr.s_addr = htonl(INADDR_ANY);
            if (setsockopt(muxFd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes))) {
                printlog("Failed to set SO_BROADCAST on NMEA socket: %s", strerror(errno));
                close(muxFd);
                return 1;
            }
        }

        if (socketCast == IP_MULTICAST_IF) {
            // use setsockopt() to request that the kernel join a multicast group
            mreq.imr_multiaddr.s_addr = inet_addr(str);
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            if (setsockopt(muxFd, IPPROTO_IP,IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
               printlog("Failed to add  multicast membership on NMEA socket: %s", strerror(errno));
               close(muxFd);
               return 1;
            }
        }

        // Bind to receive address
        if (bind(muxFd, (struct sockaddr *) &peer_sa, sizeof(peer_sa)) < 0) {
           printlog("Failed to bind to UDP address on NMEA socket: %s", strerror(errno));
           close(muxFd);
           return 1;
        } 

    } else if (!kplex_fork) {
        if (connect(muxFd, (struct sockaddr*)&peer_sa, sizeof(peer_sa)) < 0) {
            printlog("Failed to connect to NMEA socket: %s : is a mux (kplex) running?", strerror(errno));
            return 1;
        }
    }

    // Set socket nonblocking
    flags = fcntl(muxFd, F_GETFL, NULL);
    flags |= O_NONBLOCK;
    fcntl(muxFd, F_SETFL, flags);

    return 0;
}

#ifdef MT1800
// Open the watermaker socket
static void wm_sock_open()
{
    struct ip_mreq mreq;
    u_int yes = 1;  
 
    // create datagram socket
    if ( (wmsockFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) > 0) {

        // allow multiple sockets to use the same PORT number
        if (setsockopt(wmsockFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            printlog("Failed to set REUSEADDR on WM-socket %s", strerror(errno));
            close(wmsockFd);
            wmsockFd=0;
        }

        // fill in host sockaddr_in
        memset(&wmpeer_sa, 0, sizeof(struct sockaddr_in));
        wmpeer_sa.sin_family = AF_INET;
        wmpeer_sa.sin_addr.s_addr = inet_addr(WMMULTIADDR);
        wmpeer_sa.sin_port = htons(WMPORT);

        // bind to receive address
        if (wmsockFd>0 && bind(wmsockFd, (struct sockaddr *) &wmpeer_sa, sizeof(wmpeer_sa)) < 0) {
           printlog("Failed to bind to WM-socket: %s", strerror(errno));
           close(wmsockFd);
           wmsockFd=0;
        }

        // use setsockopt() to request that the kernel join a multicast group
        mreq.imr_multiaddr.s_addr = inet_addr(WMMULTIADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (wmsockFd>0 && setsockopt(wmsockFd, IPPROTO_IP,IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
           printlog("Failed to set socket options on WM-socket: %s", strerror(errno));
           close(wmsockFd);
           wmsockFd=0;
        }
    }
}
#endif

// The configuration database is maintained from the WEB GUI settings dialogue.
static int configure(int kpf)
{
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail, *cast;
    char *ptr, buf[100];
    int rval = 0;
    int i, fd;
    struct stat sb;
    
    // Defaults
    iconf.map_zoom = 12;
    iconf.map_updt = 6;
    iconf.depth_vwrn = 4;
    iconf.client_ip[0] = '\0';

    // If kplex.conf and/or a config database file is missing, create templates so that
    // we keep the system going. The user has then to create his own profile from the web gui.

    if (kpf) {
        if (stat(KPCONFPATH, &sb) <0) {
            if (errno == ENOENT) {
                char str[200];
                sprintf(str, "[file]\nfilename=%s\ndirection=in\neol=rn\npersist=yes\n\n[tcp]\nmode=server\naddress=127.0.0.1\nport=%d\n", FIFOPNMEA,  NMEAPORT);
                printlog("KPlex configuration file is missing. Creating an empty config file as %s ", KPCONFPATH);
                if ((fd=open(KPCONFPATH, O_CREAT | O_WRONLY, 0664)) < 0) {
                    printlog("Attempt to create %s failed: %s", KPCONFPATH, strerror(errno));
                    return 1;
                }
                unusedInt = write(fd, str, strlen(str));
                (void)close (fd);
                unusedInt = chown(KPCONFPATH, (uid_t)UID, (gid_t)GID);
            } else {
                printlog("Cannot access %s: %s: ", KPCONFPATH, strerror(errno));
                return 1;
            }
        }
    }
    
    memset(&sb, 0, sizeof(struct stat));

    (void)stat(NAVIDBPATH, &sb);
    rval = errno;

    if (sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READONLY, 0) || sb.st_size == 0) {
        (void)sqlite3_close(conn);

        if (sb.st_size == 0) rval = ENOENT;

        if (!sb.st_size) {

            switch (rval) {
                case EACCES:
                    printlog("Configuration database %s: ", strerror(rval));
                    return 1;
                case ENOENT:
                    printlog("Configuration database does not exist. A new empty database will be created");
                    (void)sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE , 0);
                    if (conn == NULL) {
                            printlog("Failed to create a new database %s: ", (char*)sqlite3_errmsg(conn));
                            return 1;
                    }

                    sqlite3_prepare_v2(conn, "CREATE TABLE rev(Id INTEGER PRIMARY KEY,  rev TEXT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE auth(Id INTEGER PRIMARY KEY,  password TEXT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE gmap(Id INTEGER PRIMARY KEY, zoom INTEGER, updt INTEGER, key TEXT)", -1, &res, &tail);
                    sqlite3_step(res);
  
                    sqlite3_prepare_v2(conn, "CREATE TABLE ttys (Id INTEGER PRIMARY KEY,  name TEXT, baud TEXT, dir TEXT, use TEXT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE depth (Id INTEGER PRIMARY KEY, vwrn INTEGER, tdb DECIMAL)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE sumlog (Id INTEGER PRIMARY KEY, display INTEGER, cal INTEGER)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE netif (Id INTEGER PRIMARY KEY, device TEXT, port TEXT, addr TEXT, type TEXT, proto TEXT, use TEXT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE file (Id INTEGER PRIMARY KEY, fname TEXT, rate INTEGER, use TEXT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE ais (Id INTEGER PRIMARY KEY, aisname TEXT, aiscallsign TEXT, aisid BIGINT, aisuse INTEGER, ro INTEGER)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE abuddies (Id INTEGER PRIMARY KEY, userid BIGINT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE devadc(Id INTEGER PRIMARY KEY, device TEXT, relay1txt TEXT, relay2txt TEXT, relay3txt TEXT, relay4txt TEXT)", -1, &res, &tail);
                    sqlite3_step(res);
                    
                    sprintf(buf, "INSERT INTO devadc (device) VALUES ('%s')", "/dev/null");
                    sqlite3_prepare_v2(conn, buf, -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE devadcrelay(Id INTEGER PRIMARY KEY, relay1tmo INTEGER, relay2tmo INTEGER, relay3tmo INTEGER, relay4tmo INTEGER)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO devadcrelay (relay1tmo,relay2tmo,relay3tmo,relay4tmo) VALUES (0,0,0,0)", -1, &res, &tail);
                    sqlite3_step(res);
                
#ifdef REV
                    sprintf(buf, "INSERT INTO rev (rev) VALUES ('%s')", REV);
#else
                    sprintf(buf, "INSERT INTO rev (rev) VALUES ('unknown')");
#endif
                    sqlite3_prepare_v2(conn, buf, -1, &res, &tail);
                    sqlite3_step(res);

                    sprintf(buf, "INSERT INTO auth (password) VALUES ('%s')", DEFAULT_AUTH);
                    sqlite3_prepare_v2(conn, buf, -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO gmap (zoom,updt) VALUES (14,6)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO depth (vwrn,tdb) VALUES (4,1)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO sumlog (display,cal) VALUES (1,2)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO ais (aisname,aiscallsign,aisid,aisuse,ro) VALUES ('my yacht','my call',366881180,1,0)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO file (fname,rate,use) VALUES ('nofile',1,'off')", -1, &res, &tail);
                    sqlite3_step(res);

                    for (i=0; i < MAX_TTYS; i++) {
                        sqlite3_prepare_v2(conn, "INSERT INTO ttys (name,baud,dir,use) VALUES ('undef','4800','in','off')", -1, &res, &tail);
                        sqlite3_step(res);
                    }
                    for (i=0; i < MAX_NICS; i++) {
                        sqlite3_prepare_v2(conn, "INSERT INTO netif (device,port,addr,type,proto,use) VALUES ('undef','10110','127.0.0.1','unicast','tcp','off')", -1, &res, &tail);
                        sqlite3_step(res);
                    }
                    unusedInt = chown(NAVIDBPATH, (uid_t)UID, (gid_t)GID);
                    rval = sqlite3_finalize(res);
                    break;
                default:
                    printlog("Configuration database initialization failed: %s. Try command line options", strerror(rval));
                    return 0;
            }
        } else {
            printlog("Failed to handle configuration database %s: ", (char*)sqlite3_errmsg(conn));
            (void)sqlite3_close(conn);
            return 0;
        }
    }

#ifdef REV
    // Check revision of database
    rval = sqlite3_prepare_v2(conn, "select rev from rev", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            if(strcmp((ptr=(char*)sqlite3_column_text(res, 0)), REV)) {
                printlog("Warning: Database version missmatch in %s", NAVIDBPATH);
                printlog("Expected %s but current revision is %s", REV, ptr);
                printlog("You may have to remove %s and restart this program to get it rebuilt and then configure the settings from the GUI", NAVIDBPATH);
            }
    }
#endif

    // Google Map Service 
    rval = sqlite3_prepare_v2(conn, "select zoom,updt from gmap", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            iconf.map_zoom = sqlite3_column_int(res, 0);
            iconf.map_updt = sqlite3_column_int(res, 1);
    }
    // Depth
    rval = sqlite3_prepare_v2(conn, "select vwrn from depth", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            iconf.depth_vwrn = sqlite3_column_int(res, 0);
    }
    // Transponder Depth
    rval = sqlite3_prepare_v2(conn, "select tdb from depth", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            iconf.depth_transp = sqlite3_column_double(res, 0);
    }

    // ADC device
    rval = sqlite3_prepare_v2(conn, "select device from devadc", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            (void)strcpy(iconf.adc_dev, (char*)sqlite3_column_text(res, 0));
    }

    // AIS
    rval = sqlite3_prepare_v2(conn, "select aisname,aisid,aisuse from ais", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            (void)strcpy(aisconf.my_name, (char*)sqlite3_column_text(res, 0));
            aisconf.my_userid = (long)sqlite3_column_int(res, 1);
            aisconf.my_useais = sqlite3_column_int(res, 2);
    }
    rval = sqlite3_prepare_v2(conn, "select name from ttys where dir = 'Ais' limit 1", -1, &res, &tail);
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
        (void)strcpy(iconf.ais_dev, (char*)sqlite3_column_text(res, 0));
        printlog("   AIS device:  %s", iconf.ais_dev);
    } else iconf.ais_dev[0]= '\0';

    // Authentication for GUI
    rval = sqlite3_prepare_v2(conn, "select password from auth", -1, &res, &tail);
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
        (void)strcpy(iconf.md5pw, (char*)sqlite3_column_text(res, 0));
        iconf.m55pw_ts = 0;
    } else iconf.md5pw[0]= '\0';

    // Still in file feed config mode?
    if ((fd=open(KPCONFPATH, O_RDONLY)) > 0) {    
        if (read(fd, buf, sizeof(buf)) >0 && strstr(buf, FIFOKPLEX) != NULL) {
            rval = sqlite3_prepare_v2(conn, "select fname,rate from file", -1, &res, &tail);        
            if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                    (void)strcpy(iconf.fdn_inf, (char*)sqlite3_column_text(res, 0));
                    lineRate = sqlite3_column_int(res, 1);
            }            
            if (access( FIFOKPLEX, F_OK ) == -1) { // Must exist from now on ..
                if (mkfifo(FIFOKPLEX, (mode_t)0664)) {
                    printlog("Error create kplex fifo: %s", strerror(errno));
                }
            }
        }
        (void)close(fd);
    }

    // Server network properties if not set on command line
    rval = sqlite3_prepare_v2(conn, "select device,addr,port,proto,type from netif where use = 'on'", -1, &res, &tail);        
    if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
        if (backGround) printlog("Setting network properties from configuration database:");
        if (peer_sa.sin_addr.s_addr == 0) { 
            peer_sa.sin_addr.s_addr = inet_addr((char*)sqlite3_column_text(res, 1));
            printlog("   I.P address: %s", (char*)sqlite3_column_text(res, 1));
        } 
        if (ntohs(peer_sa.sin_port) == 0) {
            peer_sa.sin_port = htons(atoi((char*)sqlite3_column_text(res, 2)));
            printlog("   Port:        %s", (char*)sqlite3_column_text(res, 2));
        }
        if (socketType == 0) {
            if (!strncmp((char*)sqlite3_column_text(res, 3),"udp", 3))
                socketType = SOCK_DGRAM;
            else
                socketType = SOCK_STREAM;
            printlog("   Protocol:    %s", (char*)sqlite3_column_text(res, 3));
        }
        if (socketType == SOCK_DGRAM) {
            if (!strlen(interFace)) {
                (void)strcpy(interFace, (char*)sqlite3_column_text(res, 0));
                printlog("   Interface:   %s", (char*)sqlite3_column_text(res, 0));
            }
            if (!socketCast) {
                cast = (char*)sqlite3_column_text(res, 4);
                printlog("   Type:        %s", cast);
                if (!strcmp("unicast", cast)) socketCast = IP_UNICAST_IF;
                else if (!strcmp("multicast", cast)) socketCast = IP_MULTICAST_IF;
                else if (!strcmp("broadcast", cast)) socketCast = SO_BROADCAST;
                else socketCast = IP_UNICAST_IF;
            }
        }
    } else {
        printlog("Autoconfig of server network properties not done. The config database needs to be populated from the GUI");
    }
    
    rval = SQLITE_BUSY;
    
    while(rval == SQLITE_BUSY) { // make sure ! busy ! db locked 
        rval = SQLITE_OK;
        sqlite3_stmt * res = sqlite3_next_stmt(conn, NULL);
        if (res != NULL) {
            rval = sqlite3_finalize(res);
            if (rval == SQLITE_OK) {
                rval = sqlite3_close(conn);
            }
        }
    }
    (void)sqlite3_close(conn);
   
    return 0; 
}

static void handle_ais_buddy(long userid)
{
    sqlite3 *conn;
    sqlite3_stmt *res;
    const char *tail;
    char sql[60];

    (void)sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READWRITE, 0);
    if (conn == NULL) {
        printlog("Failed to open database %s to add a buddy: ", (char*)sqlite3_errmsg(conn));
        aisconf.my_buddy = 0;
        return;
    }

    (void)sprintf(sql, "SELECT userid FROM abuddies WHERE userid = %ld", userid);
    if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
        (void)sqlite3_finalize(res);
        (void)sprintf(sql, "DELETE FROM abuddies WHERE userid = %ld", userid);
        if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK) {
            (void)sqlite3_step(res);
        } else userid = 0;
        (void)sqlite3_finalize(res);
    } else {
        (void)sqlite3_finalize(res);
        (void)sprintf(sql, "INSERT INTO abuddies (userid) VALUES (%ld)", userid);  
        if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) ==  SQLITE_OK) {
            (void)sqlite3_step(res);
        } else  userid = 0;
        (void)sqlite3_finalize(res);       
    }

    (void)sqlite3_close(conn);

    aisconf.my_buddy = userid;  // Leave it to be picked up later by addShip()
}

// Save NMEA stream to file
static void saveNmea(void)
{
    FILE *fd;
    char *ptr;
    int mins=0;
    char fname[PATH_MAX];

    iconf.fdn_stream = NULL;
    iconf.fdn_starttime = iconf.fdn_endtime= 0;

    if (!strlen(iconf.fdn_outf))
        return;

    if ((ptr=strchr(iconf.fdn_outf, ':')) == NULL) {
        iconf.fdn_outf[0] = '\0';
        return;
    }
    ptr[0] = '\0';
    if ((mins=atoi(++ptr)) <= 0) {
        iconf.fdn_outf[0] = '\0';
        return;
    }

    sprintf(fname, "%s/%s", UPLOADPATH, iconf.fdn_outf);
    if ((fd=fopen(fname, "w")) == NULL) {
        iconf.fdn_outf[0] = '\0';
        return;
    }
    printlog("Begin recording %d minutes of the NMEA stream to %s", mins, fname);
    iconf.fdn_starttime = time(NULL);
    iconf.fdn_endtime = iconf.fdn_starttime+mins*60;
    iconf.fdn_stream = fd;
}

// SRT proprietary AIS commands
static void aisSet(int status)
{
    struct stat sb;
    int fd;
    char buf[100];

    if (!strlen(iconf.ais_dev))
        return;

    if (stat(iconf.ais_dev, &sb)) {
        printlog("Error stat AIS device %s : %s", iconf.ais_dev, strerror(errno));
        return;
    }

    if ((fd=open(iconf.ais_dev, O_WRONLY)) < 0) {
        printlog("Error open AIS device %s : %s", iconf.ais_dev, strerror(errno));
        return;
    }

    memset(buf, 0, sizeof(buf));

    // Turn AIS transmitter on/off

    printlog("Set AIS Transmitter %s", status==1? "on":"off");

    if (!status)
        sprintf(buf, AISTRXOFF);
    else
        sprintf(buf, AIXTRXON);

    write(fd, buf, strlen(buf));

    close(fd);

}

#define MAX_LONGITUDE 180
#define MAX_LATITUDE   90

static float dms2dd(float coordinates, const char *val)
{ // Degrees Minutes Seconds to Decimal Degrees

    // Check limits
    if ((*val == 'm') && (coordinates < 0.0 && coordinates > MAX_LATITUDE)) {
        return 0;    
    }
    if (*val == 'p' && (coordinates < 0.0 && coordinates > MAX_LONGITUDE)) {
          return 0;
    }
   int b;   //to store the degrees
   float c; //to store de decimal
 
   // Calculate the value in format nn.nnnnnn
 
   b = coordinates/100;
   c= (coordinates/100 - b)*100 ;
   c /= 60;
   c += b;
   
   return c;
}

// returns the true wind speed given boat speed, apparent wind speed and apparent wind direction in degrees
// https://github.com/drasgardian/truewind
double trueWindSpeed(double boatSpeed, double apparentWindSpeed, double apparentWindDirection)
{
    // convert degres to radians
    double apparentWindDirectionRadian = apparentWindDirection * (M_PI/180);

    return pow(pow(apparentWindSpeed*cos(apparentWindDirectionRadian) - boatSpeed,2) + (pow(apparentWindSpeed*sin(apparentWindDirectionRadian),2)), 0.5);
}

// returns the true wind direction given boat speed, apparent wind speed and apparent wind direction in degrees
// https://github.com/drasgardian/truewind
double trueWindDirection(double boatSpeed, double apparentWindSpeed, double apparentWindDirection)
{

    int convert180 = 0;
    double twdRadians;
    double apparentWindDirectionRadian;
    double twdDegrees;

    // formula below works with values < 180
    if (apparentWindDirection > 180) {
        apparentWindDirection = 360 - apparentWindDirection;
        convert180 = 1;
    }

    // convert degres to radians
    apparentWindDirectionRadian = apparentWindDirection * (M_PI/180);

    twdRadians = (90 * (M_PI/180)) - atan((apparentWindSpeed*cos(apparentWindDirectionRadian) - boatSpeed) / (apparentWindSpeed*sin(apparentWindDirectionRadian)));

    // convert radians back to degrees
    twdDegrees = twdRadians*(180/M_PI);
    if (convert180) {
        twdDegrees = 360 - twdDegrees;
    }

    return twdDegrees;
}


#define windowBits 15
#define GZIP_ENCODING 16

static int strm_init(z_stream * strm)
{
    strm->zalloc = Z_NULL;
    strm->zfree  = Z_NULL;
    strm->opaque = Z_NULL;
    return deflateInit2 (strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                             windowBits | GZIP_ENCODING, 8,
                             Z_DEFAULT_STRATEGY);
}


static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len)
{
    return 0;
}

// Handle all requests from remote virtual instruments.

static int callback_nmea_parser(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len)
{
    int  i, cnt, req, rval, maxz = 0;
    char value [MAX_LWSZ];
    unsigned char gzout[MAX_LWSZ];
    unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_LWSZ + LWS_SEND_BUFFER_POST_PADDING];
    char *args = NULL;
    time_t ct = time(NULL);
    z_stream strm;

    memset(value, 0, MAX_LWSZ);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
#if 0
            printlog("Connection established");
#endif
            break;

        case LWS_CALLBACK_RECEIVE: {

            char clientName[40];
            char clientIp[20] = { '\0' };

            lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi), clientName, sizeof(clientName), clientIp, sizeof(clientIp));

            if ((args=index(in, '-')) != NULL)
                *args++ = '\0';

            switch ((req=atoi((char*)in)))
            {   // Handle client (virtual instruments) enumerated requests.
                // Reply with simple JSON 
                // js example:
                // send(Cmd.SpeedOverGround); // WebSocket send
                // Reply from us: Exp-SpeedOverGround (expired server data) or:
                // Json reply {"speedog":"4.46"}-SpeedOverGround
                // where tail -SpeedOverGround and Exp-SpeedOverGround is for
                // target validation to be split out before parsing the json string.
         
                case SpeedOverGround: {
                    if (ct - cnmea.rmc_ts > INVALID || cnmea.rmc < 0.9)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'speedog':'%.2f'}-%d",cnmea.rmc, req);
                    break;
                }
               
                case SpeedThroughWater: {
                    if (ct - cnmea.stw_ts > INVALID || cnmea.stw < 0.9)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'sppedtw':'%.2f'}-%d", cnmea.stw, req);
                    break;
                }
               
                case DepthAndTemp: {
                    if (ct - cnmea.dbt_ts > INVALID)
                        sprintf(value, "Exp-%d", req);
                    else {
                        if (ct - cnmea.mtw_ts > INVALID) cnmea.mtw=0;
                        sprintf(value, "{'depth':'%.2f','vwrn':'%d','temp':'%.1f'}-%d", \
                            cnmea.dbt,iconf.depth_vwrn, cnmea.mtw, req);
                        }
                    break;
                }
           
                case CompassHeading: {
                    if (ct - cnmea.hdm_ts > INVALID)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'angle':'%.2f'}-%d", cnmea.hdm, req);
                    break;
                }

                case GPS: {
                    if (ct - cnmea.gll_ts > INVALID)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'la':'%f','lo':'%f','N':'%s','E':'%s','A':'%.0f'}-%d", \
                            dms2dd(atof(cnmea.gll),"m"),dms2dd(atof(cnmea.glo),"m"), \
                            cnmea.glns,cnmea.glne,cnmea.hdm,req);
                    break;
                }

                case WindSpeedAndAngle: {
                    if (ct - cnmea.vwr_ts > INVALID)
                        sprintf(value, "Exp-%d", req);
                    else {
                        if (ct - cnmea.vwt_ts > INVALID) cnmea.vwta = cnmea.vwts = 0.0;
                        sprintf(value, "{'dir':'%d','angle':'%.1f','tangle':'%.1f','speed':'%.1f','tspeed':'%.1f'}-%d", \
                            cnmea.vwrd, cnmea.vwra, cnmea.vwta, cnmea.vwrs, cnmea.vwts,req);
                    }
                    break;
                }
               
                case GoogleMapFeed: {
                    if (ct - cnmea.gll_ts > INVALID*4)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'la':'%f','lo':'%f','N':'%s','E':'%s','A':'%.0f','zoom':'%d','updt':'%d','myname':'%s'}-%d", \
                            dms2dd(atof(cnmea.gll),"m"),dms2dd(atof(cnmea.glo),"m"), \
                            cnmea.glns,cnmea.glne,cnmea.hdm,iconf.map_zoom,iconf.map_updt,aisconf.my_name,req);
                   break;
                }

               case GoogleAisFeed: {

                    char gbuf[80];
                    struct aisShip_struct *ptr, *dptr;
                    struct aisShip_struct *head;
                    head = getShips(MAX_LWSZ-100);
                               
                    int glen = sprintf(gbuf, ",'N':'%s','E':'%s'},{", cnmea.glns, cnmea.glne);              
                  
                    if (head && glen) {

                        strcpy(value, "[{");
                        ptr = head;

                        while(ptr->next != NULL) { // Assemble the vessels into a JSON array
                            if (strlen(value)+glen+strlen(ptr->js)+10 < MAX_LWSZ) {
                                strcat(value, ptr->js);
                                strcat(value, gbuf);
                            }
                            free(ptr->js);
                            dptr = ptr;
                            ptr = ptr->next;
                            free(dptr);
                        }

                        sprintf(gbuf, "]-%d", req);
                        value[strlen(value)-2] = '\0';
                        strcat(value, gbuf);

                    } else sprintf(value, "Exp-%d", req);

                    break;
                }

                case GoogleAisBuddy: {
                        if (args != NULL && strlen(args)) {
                            handle_ais_buddy(atol(args));
                        }
                        break;
                }
                
                /*       --- NON NMEA SECTION  ---     */
                case SensorVolt: {
                    if (ct - cnmea.volt_ts > INVALID*10)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'volt':'%.1f'}-%d", cnmea.volt, req);
                    break;
                }

                case SensorCurr: {
                    if (ct - cnmea.curr_ts > INVALID*10)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'curr':'%.1f'}-%d", cnmea.curr, req);
                    break;
                }

                case SensorTemp: {
                    if (ct - cnmea.temp_ts > INVALID*10)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'temp':'%.1f'}-%d", cnmea.temp, req);
                    break;
                }

                case Authentication: {
                    if (args != NULL && strlen(args)) {
                        int autval = 2;

                        if (strlen(clientIp) && !strcmp(clientIp, iconf.client_ip) && !strncmp(args, iconf.md5pw, 32)) {
                            autval = 3; // Renew auth
                            iconf.m55pw_ts = time(NULL)+DEFAULT_AUTH_DURATION;
                        } else {
                        
                            if (!strlen(iconf.client_ip)) {  
                                if (!strlen(clientIp)) {
                                    autval = 2;
                                } else {
                                    autval = strncmp(args, iconf.md5pw, 32) == 0? 3:2;
                                    if (autval == 3) {
                                        strcpy(iconf.client_ip, clientIp);
                                        iconf.m55pw_ts = time(NULL)+DEFAULT_AUTH_DURATION;
                                    } else autval = 2;
                                }                                

                            } else autval = 2;
                        }

                        sprintf(value, "{'Authen':'%d'}-%d", autval, req);
                        printlog("Authentication for client %s %s", clientIp, autval == 3? "granted" : "denied");
                    }
                    break;
                }

                case StatusReport: {
                    int auttmo = 4;
                    cnmea.txs = ct - cnmea.txs_ts > INVALID*2? -1:cnmea.txs;

                    if (args != NULL && strlen(args)) {
                        if (strlen(clientIp) && !strcmp(clientIp, iconf.client_ip)) {
                            auttmo = iconf.m55pw_ts > ct? 3:4;
                            if (auttmo == 4) {
                                printlog("Authentication for client %s closed", clientIp);
                                iconf.client_ip[0] = '\0';
                                iconf.m55pw_ts = 0;
                            }
                        }
                    }

                    sprintf(value, "{'relaySts':'%d','tdtSts':'%d','aisTxSts':'%d','nmRec':'%s','nmPlay':'%s','Authen':'%d'}-%d",
                        relayStatus(), smartplugGet(), cnmea.txs, iconf.fdn_outf, fileFeed==1? basename(iconf.fdn_inf):"", auttmo, req);

                    break;
                }

                case SensorRelay: {
                    if (args != NULL && strlen(args)) {
                        relaySet(atoi(args));
                        sprintf(value, "{'relaySet':'%d'}-%d", relayStatus(), req);
                    }
                    break;
                }

#ifdef MT1800
                case WaterMakerData: {
                    if (ct - cnmea.cond_ts > INVALID*10)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value,
                            "{'condu':'%.1f','temp':'%.1f','quality':'%d','flowh':'%d','flowt':'%d','pvol':'%d','runs':'%d'}-%d", \
                            cnmea.cond, cnmea.ctemp, cnmea.cwqvalue, cnmea.cflowh, cnmea.cflowt, cnmea.cflowp, cnmea.crunsts,req);
                    break;
                }
#endif
                case AisTrxStatus: {
                    sprintf(value, "{'aisSet':'%d'}-%d", 0, req);
                    if (args != NULL && strlen(args)) {
                        aisSet(atoi(args));
                    }
                    break;
                }

                case tdtStatus: {
                    sprintf(value, "{'tdtSet':'%d'}-%d", 0, req);
                    if (args != NULL && strlen(args)) {
                        smartplugSet(atoi(args));
                    }
                    break;
                }

                case SaveNMEAstream: {           
                    sprintf(value, "{'saveNmea':'%d'}-%d", 0, req);
                    if (args != NULL && !strncmp(args, "ABORT", 5)) {
                        iconf.fdn_endtime = 0;
                    } else  if (args != NULL && strlen(args)) {
                        strcpy(iconf.fdn_outf, args);
                        saveNmea();
                    }
                    break;
                }

                case ServerPing: { // Diagnostics: ping kplex/talkers presence with RMC
                        sprintf(value, "{'status':'%d'}-%d", ct-cnmea.rmc_ts > INVALID?0:1, req);
                    break;
                }

                case TimeOfDAy: { // Diagnostics: Time of Day
                        struct tm tm = *localtime(&ct);
                        sprintf(value, "{'hours':'%d','minutes':'%d','seconds':'%d'}-%d",\
                            tm.tm_hour, tm.tm_min, tm.tm_sec,req);
                    break;
                }

                case UpdateSettings: { // Update Instrument settings from db
                    (void)configure(0);;
                    sprintf(value, "{'status':'%d'}-%d", ct-cnmea.rmc_ts > INVALID?0:1,req);
                    break;
                }
                
                default:
                    if(debug)
                        printlog("Unknown command request: %d", req);
                    break;
           }

           if (!(rval = strlen(value))) { return 0;}
           
           for (i=0; i<rval; i++) // Format valid JSON
                if (value[i] == '\'') value[i] = '"';

           // Log what we recieved and what we're going to send as a response.
           if (debug) {
                printlog("received command: %s, replying: %s, count=%d", (char *)in, value, rval);
           }

           // Now compress this data
           if (strm_init(& strm) < 0) {
                printlog("strm_init: failed");
                return  -1;
           }

           strm.next_in = (unsigned char *)value;
           strm.avail_in = rval;
           i=LWS_SEND_BUFFER_PRE_PADDING;
           maxz = WS_FRAMEZ - (LWS_SEND_BUFFER_PRE_PADDING+LWS_SEND_BUFFER_POST_PADDING);
           cnt = 0;

           do {
                int have, j;
                strm.avail_out = maxz;
                strm.next_out = gzout;
                if (deflate (& strm, Z_FINISH) < 0) {
                    printlog("deflate: failed");
                    deflateEnd (& strm);
                    return  -1;
                }
                have = maxz - strm.avail_out;
                for(j=0; j < have; j++)
                    buf[i++] = gzout[j];
                 cnt+=have;
           }
           while (strm.avail_out == 0);
           deflateEnd (& strm);          

           // Send response.
           cnt = lws_write(wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], cnt, LWS_WRITE_BINARY);

           if (cnt < 0)
               return -1;

           break;
       }
       default:
           break;
    }

    return 0;
}

static struct lws_protocols protocols[] = {
    // First protocol must always be HTTP handler
    {
        "http-only",    // name
        callback_http,  // callback
        0               // per_session_data_size
    },
    {
        "nmea-parser-protocol", // protocol name - very important!
        callback_nmea_parser,   // callback
        0,                      // we don't use any per session data
        WS_FRAMEZ               // Max frame size
    },
    {
        NULL, NULL, 0   /* End of list */
    }
};

// Timestamp in ms
static long long ms_timestamp(void) {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

// "$P". These extended messages are not standardized. 
// Custom NMEA(P) going out to kplex input fifo
static void *threadPnmea_run()
{
    static int rv;
    int pnmeafd = 0;
    float pkWhp = 0;
    float npkWp = 0;
    float pkWhn = 0;
    float npkWn = 0;
    time_t samplesp = 1;
    time_t samplesn = 1;
    long long downP = 0;
    long long downN = 0;
    long long msStartTime = 0;
    long long msUpTime = 0;
    long long mSts = 0;
    long long prevmSts = 0;

    char fifobuf[60];
    char outbuf[70];

     if ((pnmeafd = open(FIFOPNMEA, O_RDWR)) < 0) {
        pNmeaStatus = 0;
        printlog("Error open fifo for p-type data: %s", strerror(errno));
    }

    msStartTime = ms_timestamp();
    cnmea.startTime = msStartTime / 1000;

    cnmea.kWhp = cnmea.kWhn = 0;

    while (pNmeaStatus == 1)
    {
        mSts = ms_timestamp();     // Get a timestamp for this turn
        msUpTime = mSts - msStartTime; 
        cnmea.upTime = msUpTime / 1000;

        float volt = cnmea.volt;
        float curr = cnmea.curr;

        if (!(mSts/1000 - cnmea.volt_ts > INVALID*10)) {

            int checksum = 0;
            int i = 1; 

            if (curr >= 0) {
                if (volt*curr > 0.2) {
                    npkWp = (pkWhp * (samplesp-1) + volt * curr) / samplesp;
                    pkWhp = npkWp;
                    cnmea.kWhp = ((npkWp * (msUpTime - downP)) / 3600000) / 1000;
                    samplesp++;
                } else downP += mSts - prevmSts;
            } else {
                if (volt*fabs(curr) > 0.2) {
                    npkWn = (pkWhn * (samplesn-1) + volt * fabs(curr)) / samplesn;
                    pkWhn = npkWn;
                    cnmea.kWhn = ((npkWn * (msUpTime - downN)) / 3600000) / 1000;
                    samplesn++;
                } else downN += mSts - prevmSts;
            }

            // Format: GPENV,volt,bank,current,bank,temp,where,kWhp,kWhn,startTimr*cs
            sprintf(fifobuf, "$GPENV,%.1f,1,%.1f,1,%.1f,1,%.3f,%.3f,%lu", cnmea.volt, cnmea.curr, cnmea.temp, cnmea.kWhp, cnmea.kWhn, cnmea.startTime);

            while(fifobuf[i] != '\0')
                checksum = checksum ^ fifobuf[i++];

            sprintf(outbuf,"%s*%x\r\n", fifobuf, checksum);
            write(pnmeafd, outbuf, strlen(outbuf));
#ifdef XDRTEST
            sprintf(fifobuf, "$IIXDR,U,%.1f,V,VAH30", cnmea.volt);
            i=1;
            while(fifobuf[i] != '\0')
                checksum = checksum ^ fifobuf[i++];

            sprintf(outbuf,"%s*%x\r\n", fifobuf, checksum);
            write(pnmeafd, outbuf, strlen(outbuf));
#endif
        }
        prevmSts = ms_timestamp();
        sleep(1);
    }

    if (pnmeafd > 0)
        close(pnmeafd);

    pthread_exit(&rv);
}

// Make KPlex a persistent child of us
static void *threadKplex_run()
{
    time_t stt = 0;
    int retry = 5;
    static int rv;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    sleep(4);
 
    while (kplexStatus == 0)
    { // Kplex angel function
        if (time(NULL)-stt <3) {
            if (!--retry) {
                printlog("KPLEX respawning to quickly - giving up");
                kplexStatus = ESRCH;
                break;
            }
        }
        printlog("Starting KPLEX multiplexer");
        
        pidKplex = fork();
        
        if (pidKplex == 0) {  
            /* child */
            sleep(1);
            char *args[] = {"kplex", "-f", KPCONFPATH, NULL};
            execvp("kplex", args);
            printlog("Failed to execute kplex %s:", strerror(errno));
            exit(errno);
        } else {
            // (Re)connect to NMEA socket if tcp
            sleep(2);
            if (!kill(pidKplex, 0) && socketType == SOCK_STREAM) {
                if (nmea_sock_open(1)) {
                    kplexStatus = ENOTCONN;
                    break;
                }
                sleep(1);
                if (connect(muxFd, (struct sockaddr*)&peer_sa, sizeof(peer_sa)) < 0) {
                    if (errno != EINPROGRESS) {
                        printlog("Failed to connect to NMEA socket: %s", strerror(errno));
                        kplexStatus = ECONNABORTED;
                        break;
                    }
                }
            }  
            stt = time(NULL);
            wait(&kplexStatus);
            if (WIFEXITED(kplexStatus))
                printlog("KPLEX exited with status = %d", WEXITSTATUS(kplexStatus));
            else
                printlog("KPLEX exited with status = %d", kplexStatus);
                
            if (sigExit) break;
            if (kplexStatus) { pidKplex=0; break; }
            
            printlog("Restarting KPLEX");    
        }   
    }
    
    if (!sigExit)  // Avoid restart on SIGx
        exit_clean(WEXITSTATUS(kplexStatus));
        
    pthread_exit(&rv);
}

// Feed the fifo listened to by KPlex
static void *t_fileFeed()
{
    static int rval;
    char buff[250];
   
    FILE *fdi, *fdo;
       
    if (access( FIFOKPLEX, F_OK ) == -1) {
        if (mkfifo(FIFOKPLEX, (mode_t)0664)) {
            printlog("Error create kplex fifo: %s", strerror(errno));
            pthread_exit(&rval);
        }
    }

    if ((fdi=fopen(iconf.fdn_inf,"r")) == NULL) {
        printlog("Error open feed file: %s", strerror(errno));
        pthread_exit(&rval);
    }

    if ((fdo=fopen(FIFOKPLEX,"w+")) == NULL) {
        printlog("Error to set permission on kplex fifo: %s", strerror(errno));
        pthread_exit(&rval);
    }

    printlog("Starting File input services - rate = %d/s", lineRate);
    
    while(fileFeed) {
        memset(buff, 0, sizeof (buff));
        if (fgets(buff, sizeof(buff), fdi) != NULL) {
            fputs(buff, fdo);fflush(fdo);
        }
        if (feof(fdi)) rewind(fdi);
            nanosleep((struct timespec[]){{0, (1000000000L-1)/lineRate}}, NULL);
    }

    fclose(fdi); fclose(fdo);

    printlog("Stopping File input services");
    pthread_exit(&rval);
}

void usage(char *prg)
{
    fprintf(stderr, "Usage: %s [ -a nmea-mux ip-address ] [ -p port ] [-w websocket port ]\n [ -s 1/2/3 ] [ -i nic ] [ -n don't fork kplex ] [ -b background ]\n [ -v version ] [ -f file ] [ -r rate ] [ -u UID ] [ -g GID ] [ -d debug ]\n", prg);
    fprintf(stderr, "Where -s 1=unicast, 2=multicast, 3=broadcast UDP sockets\n");
    fprintf(stderr, "Defaults from configuration database:\n");
    configure(0);
}

int main(int argc ,char **argv)
{
    int wsport;
    char nmeastr_p1[512];
    char nmeastr_p2[512];
    ais_state ais;
    int opts = 0;
    int c;
    int kplex_fork = 1;
    int detachstate;
    pid_t pid;
    pthread_attr_t attr;  
    pthread_t t1;
    pthread_t t2;
    struct sigaction action;
    struct lws_context_creation_info info;
    const char *cert_path = NULL;
    const char *key_path = NULL;
    pthread_t threadPnmea = 0;

    programName = basename(argv[0]);

    // Defaults
    wsport = WSPORT;
    memset(&peer_sa, 0, sizeof(struct sockaddr_in));
    memset(interFace, 0 ,sizeof(interFace));
    peer_sa.sin_family = AF_INET;
    
    iconf.fdn_inf[0] = '\0';
    (void)unlink(WSREBOOT);

    while ((c = getopt (argc, argv, "a:bdf:hi:np:r:s:vw:")) != -1)
        switch (c)
            {
            case 'a':
                peer_sa.sin_addr.s_addr = inet_addr(argv[optind-1]);
                break;
            case 'b':
                backGround = 1;
                break;
             case 'd':
                debug = 1;
                break;          
            case 'f':
                (void)strcpy(iconf.fdn_inf, optarg);
                break;
            case 'i':
                (void)strcpy(interFace, optarg);
                break;
            case 'n':
                kplex_fork = 0;
                break;
            case 'p':
                peer_sa.sin_port = htons(atoi(optarg));
                break;
            case 'r':
                lineRate = atoi(optarg);
                break; 
            case 's':
                socketType = SOCK_DGRAM;
                switch(atoi(optarg))
                    {
                        case 1: socketCast = IP_UNICAST_IF; break;
                        case 2: socketCast = IP_MULTICAST_IF; break;
                        case 3: socketCast =  SO_BROADCAST; break;
                        default: usage(argv[0]); exit(EXIT_FAILURE);
                    }
                break;  
            case 'v':
                fprintf(stderr, "revision: %s\n", SWREV);
                exit(EXIT_SUCCESS);
                break;
            case 'w':
                wsport = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
                break;
            }

    if (geteuid() != 0) {
        fprintf(stderr, "Error: this program must start with root privileges\n");
        exit(EXIT_FAILURE);
    }

    if (strlen(iconf.fdn_inf) && !kplex_fork) {
        fprintf(stderr, "Error: incompatible options -n and -f\n");
        exit(EXIT_FAILURE);
    }

    if (kplex_fork) {
        // Make sure to restart kplex in case of config change
        FILE * cmd = popen("pidof kplex", "r");
        *nmeastr_p1 = '\0';
        if (fgets(nmeastr_p1, 20, cmd) != NULL) {
            pid = strtoul(nmeastr_p1, NULL, 10);
            while(!kill(pid, SIGINT)) {
                fprintf(stderr, "Shutting down existing kplex (%d) with SIGINT\n", (int)pid);
                usleep(700*1000);
            }
        }
        pclose(cmd);
    }

    if (backGround) {
        fprintf(stderr, "%s daemonizing. Output redirected to syslog\n", argv[0]);
        pid = fork();
        if (pid) _exit(EXIT_SUCCESS);
        unusedInt = chdir("/");
        openlog (programName, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        printlog("Starting %s", programName);
        /* Redirect standard files to /dev/null */
        unusedFd = freopen( "/dev/null", "r", stdin);
        unusedFd = freopen( "/dev/null", "w", stdout);
        unusedFd = freopen( "/dev/null", "w", stderr);
        debug = 0; // don't flood the syslog
        setlogmask (LOG_UPTO (LOG_NOTICE));
        lws_set_log_level(LLL_ERR | LLL_WARN, lwsl_emit_syslog);
    } else lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_CLIENT, NULL);
    
   // Register signal handler
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = exit_clean;
    sigaction(SIGINT,&action,NULL);
    sigaction(SIGTERM,&action,NULL); 

    // Get settings for instruments abd set network properties from db
    if (configure(kplex_fork))
        exit(EXIT_FAILURE);

    // Open NMEA socket
    if (nmea_sock_open(kplex_fork))
        exit(EXIT_FAILURE);

    // Initialize the websocket
    memset(&info, 0, sizeof info);
    info.port = wsport;
    info.iface = NULL;
    info.protocols = protocols; 
    info.ssl_cert_filepath = cert_path;
    info.ssl_private_key_filepath = key_path;  
    info.gid = -1;
    info.uid = -1;
    info.max_http_header_data = 1024;
    info.options = opts;

    if ((ws_context = lws_create_context(&info)) == NULL) {
       printlog("libwebsocket init failed");
       exit(EXIT_FAILURE);
    }
    
    printlog("Starting NMEA services");
    
    pthread_attr_init(&attr);
    detachstate = PTHREAD_CREATE_DETACHED;
    pthread_attr_setdetachstate(&attr, detachstate);
    
    // Thread for the file re-play service
    if (strlen(iconf.fdn_inf)) {  
        fileFeed = 1;
        pthread_create(&t1, &attr, t_fileFeed, NULL);         
    }

    // Thread for the smartplug status service
    pthread_create(&t2, &attr, t_smartplug, &smartplugRun); 
    if (!t2) {
        printlog("Failed to start smartplug thread");
    }        

    // Fifo to deliver NMEA $P messages
    if (access( FIFOPNMEA, F_OK ) == -1) {
        if (mkfifo(FIFOPNMEA, (mode_t)0664)) {
            printlog("Error create kplex p-type fifo: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    // Thread for KPlex services
    if (kplex_fork) {
        pthread_create(&threadKplex, &attr, threadKplex_run, NULL);
        if (!threadKplex) {
            printlog("Failed to start kplex thread");
            exit(EXIT_FAILURE);
        }
    }

    // Thread to deliver NMEA $P messages
    if (access( FIFOPNMEA, F_OK ) == 0) {
        // We want atomic reads of ENV params  
        struct sched_param parm;   
	    pthread_attr_getschedparam(&attr, &parm);
	    parm.sched_priority = sched_get_priority_max(SCHED_FIFO);;
	    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	    pthread_attr_setschedparam(&attr, &parm);  
  
        pthread_create(&threadPnmea, &attr, threadPnmea_run, NULL);
        if (!threadPnmea) {
            printlog("Failed to start threadPnmea thread");
        } else
            pthread_setschedparam(threadPnmea, SCHED_FIFO, &parm);
    }
   

#ifdef MT1800
    wm_sock_open();
#endif

#ifdef DOADC
    if (strcmp(iconf.adc_dev, "/dev/null")) {
        (void)adcInit(iconf.adc_dev, voltChannel);
        (void)adcInit(iconf.adc_dev, currChannel);
        (void)adcInit(iconf.adc_dev, crefChannel); 
        (void)adcInit(iconf.adc_dev, tempChannel);
        (void)relayInit(4);      
    }
#endif
    
    sleep(3);
   /* Clear out the structures */
    memset(&cnmea, 0, sizeof(cnmea));
    memset(&ais, 0, sizeof(ais_state));

    memset(nmeastr_p2, 0, sizeof(nmeastr_p2));

    aisconf.my_buddy = 0;
    long my_userid = 0;
    char my_callsign[40] = {'\0'};
    char my_aisname[40]  = {'\0'};
    cnmea.startTime = time(NULL);

    // Main loop, to end this server, send signal INT(^c) or TERM
    // GUI commands WSREBOOT can break this loop requesting a restart.
    // Keep the collected_nmea struct up to date with collected data
    while(1) {

        int i, cnt, cs;
        time_t ts;
        uint8_t checksum;
        socklen_t socklen = sizeof(peer_sa);
        struct stat sb;
        int ais_rval;
        long  userid = 0;
        int trueh = 0;
        int cog = 0;
        int is_vdo = 0;
        int is_vdm = 0;
        static int got_vdo;
        char *callsign = NULL;
        char *aisname = NULL;
        static int r_limit;

        // AIS message structures
        aismsg_1  msg_1;
        aismsg_2  msg_2;
        aismsg_3  msg_3;
        aismsg_4  msg_4;
        aismsg_5  msg_5;
        aismsg_18 msg_18;
        aismsg_19 msg_19;
        aismsg_21 msg_21;
        aismsg_24 msg_24;
            
        // Position in DD.DDDDDD
        double lat_dd = 0;
        double long_ddd = 0;
        double sog = 0.1;
   
        // Reboot/re-configure request from PHP Gui code
        // Only if not under control of systemd
        if (stat(WSREBOOT, &sb) == 0) {
            break;
        }

        ts = time(NULL);        // Get a timestamp for this turn
       
        do_sensors(ts, &cnmea); // Do non NMEA things
  
        // Get the next sentence from the mux and update
        // the struct of valid data for the duration of 'INVALID'

        memset(nmeastr_p1, 0, sizeof(nmeastr_p1));

        if ((cnt=strnlen(nmeastr_p2, sizeof(nmeastr_p2)))) { // There is p2 sentence(s) to take care of.
            memcpy(nmeastr_p1, nmeastr_p2, sizeof(nmeastr_p1));
            memset(nmeastr_p2, 0, sizeof(nmeastr_p2));
        } else {
            if (socketType == SOCK_STREAM) {
                cnt = recv(muxFd, nmeastr_p1, sizeof(nmeastr_p1), 0);
            } else {
                cnt = recvfrom(muxFd, nmeastr_p1, sizeof(nmeastr_p1), 0,(struct sockaddr *) &peer_sa, &socklen);
            }
        }
       
        if (cnt < 0) {
            if (errno == EAGAIN) {
                usleep(POLLRATE*1000);
                continue;
            }
            if (errno == 107) { // Transport endpoint is not connected
                sleep(3);       // kplex not ready yet ?
                continue;
            }
            printlog("Error rerading NMEA-socket: %s", strerror(errno));
            sleep(2);
            continue;
        } else if (cnt > 0) {

            cs = checksum = 0;     // Chekcsum portion at end of nmea string 
            for (i = 0; i < cnt; i++) {
                if (nmeastr_p1[i] == '*') cs=i+1;
                if (nmeastr_p1[i] == '\r' || nmeastr_p1[i] == '\n') {
                    nmeastr_p1[i] = '\0';
                    if (strnlen(&nmeastr_p1[i+2], cnt-i)) {
                        // There is more sentence(s) in the buffer.
                        memcpy(nmeastr_p2, &nmeastr_p1[i+2], cnt-i);
                    }
                    break;
                }
            }

            if (NMPARSE(nmeastr_p1, "ENV")) {
                continue;   // We are talking to our self through threadPnmea->kplex. Skip it
            }

            if (cs > 0) {
                for (i=0; i < cs-1; i++) {
                    if (nmeastr_p1[i] == '$' || nmeastr_p1[i] == '!') continue;
                    checksum ^= (uint8_t)nmeastr_p1[i];
                }
            }            
            
            if ( !cs || (checksum != (uint8_t)strtol(&nmeastr_p1[cs], NULL, 16))) {
                if (debug) {
                    printlog("Checksum error in nmea sentence: 0x%02x/0x%02x - '%s'/'%s', pos %d", \
                        checksum, (uint8_t)strtol(&nmeastr_p1[cs], NULL, 16), nmeastr_p1, &nmeastr_p1[cs], cs);
                }
                continue;
            }

            if (debug) { fprintf(stdout, "Lenght=%d message='%s'\n", (int)strlen(nmeastr_p1), nmeastr_p1); fflush(stdout); }

            // Trig the libwebsocket_service to process all waiting events with their callback functions.   
            lws_service(ws_context, 0);

            // Check if AIS VDM/VDO
            if (NMPARSE(nmeastr_p1, "VDM")) {
                is_vdm = 1;
            }
            if (NMPARSE(nmeastr_p1, "VDO")) {
                if (got_vdo)
                    continue;
                is_vdo = 1;
            }

            if (iconf.fdn_stream != NULL) {
                static int tcnt;
                int rcnt;
                strcat(nmeastr_p1, "\r\n");
                rcnt = fwrite(nmeastr_p1, 1, strlen(nmeastr_p1), iconf.fdn_stream);
                tcnt += rcnt;
                if ((rcnt == 0) || ts >= iconf.fdn_endtime) {
                    printlog("The recording of the NMEA stream ended after %.1f minutes and %d bytes", (float)(ts-iconf.fdn_starttime)/60, tcnt);
                    fclose(iconf.fdn_stream);
                    iconf.fdn_stream = NULL;
                    iconf.fdn_outf[0] = '\0';
                    iconf.fdn_starttime = iconf.fdn_endtime = tcnt = 0;
                }
            }

            if (!(is_vdm + is_vdo)) {

                // NMEA Version 2.23 - Priority parsing order and logic:
                // See https://gpsd.gitlab.io/gpsd/NMEA.html and other sources out there
                
                // RMC - Recommended minimum specific GPS/Transit data
                // RMC feed is assumed to be present at all time 
                if (NMPARSE(nmeastr_p1, "RMC")) {
                    cnmea.rmc=atof(getf(7, nmeastr_p1));
                    if (cnmea.rmc > 1.0) { // Now use GPS heading else magnetic
                        cnmea.hdm=atof(getf(8, nmeastr_p1));
                        cnmea.hdm_ts = ts; 
                    }                      
                    strcpy(cnmea.gll, getf(3, nmeastr_p1));
                    strcpy(cnmea.glo, getf(5, nmeastr_p1));
                    strcpy(cnmea.glns, getf(4, nmeastr_p1));
                    strcpy(cnmea.glne, getf(6, nmeastr_p1));
                    cnmea.gll_ts = cnmea.rmc_ts = ts;
                    continue;
                }

                // VTG - Track made good and ground speed
                if (ts - cnmea.rmc_ts > INVALID/2) { // If not from RMC
                    if (NMPARSE(nmeastr_p1, "VTG")) {
                        cnmea.rmc=atof(getf(5, nmeastr_p1));
                        if ((cnmea.hdm=atof(getf(1, nmeastr_p1))) != 0) // Track made good
                            cnmea.hdm_ts = ts;
                        continue;
                    }
                }
        
                // GLL - Geographic Position, Latitude / Longitude
                if (ts - cnmea.gll_ts > INVALID/2) { // If not from RMC
                    if (NMPARSE(nmeastr_p1, "GLL")) {
                        strcpy(cnmea.gll, getf(1, nmeastr_p1)); 
                        strcpy(cnmea.glo, getf(3, nmeastr_p1));
                        strcpy(cnmea.glns, getf(2, nmeastr_p1));
                        strcpy(cnmea.glne, getf(4, nmeastr_p1));
                        cnmea.gll_ts = ts;
                        continue;
                    }
                }

                // VHW - Water speed and magnetic heading
                if(NMPARSE(nmeastr_p1, "VHW")) {
                    if ((cnmea.stw=atof(getf(5, nmeastr_p1))) != 0)
                        cnmea.stw_ts = ts;
                   if (cnmea.rmc < 1.0) {
                        cnmea.hdm=atof(getf(1, nmeastr_p1));
                        cnmea.hdm_ts = ts;
                    }
                    continue;
                }

                if (ts - cnmea.stw_ts > INVALID && ts - cnmea.rmc_ts  <= INVALID) {
                    cnmea.stw=cnmea.rmc;    // Not entirely correct but better than a blank instrument
                    cnmea.stw_ts = ts;
                }

                if (ts - cnmea.hdm_ts > INVALID/2) { // If not from VHW or RMC

                    // HDG - Heading - Deviation and Variation 
                    if (NMPARSE(nmeastr_p1, "HDG")) {
                        cnmea.hdm=atof(getf(1, nmeastr_p1));
                        cnmea.hdm_ts = ts;
                        continue;
                    }

                    // HDT - Heading - True (obsoleted)
                    if (NMPARSE(nmeastr_p1, "HDT")) {
                        cnmea.hdm=atof(getf(1, nmeastr_p1));
                        cnmea.hdm_ts = ts;
                        continue;
                    }
                 
                    // HDM Heading - Heading Magnetic (obsoleted)
                    if (NMPARSE(nmeastr_p1, "HDM")) {
                        cnmea.hdm=atof(getf(1, nmeastr_p1));
                        cnmea.hdm_ts = ts;
                        continue;
                    }
                }

                // DPT - Depth (Depth of transponder added)
                if (NMPARSE(nmeastr_p1, "DPT")) {
                    cnmea.dbt=atof(getf(1, nmeastr_p1))+atof(getf(2, nmeastr_p1));
                    cnmea.dbt_ts = ts;
                    continue;
                }

                // DBT - Depth Below Transponder + GUI defined transponder depth
                if (ts - cnmea.dbt_ts > INVALID/2) { // If not from DPT
                    if (NMPARSE(nmeastr_p1, "DBT")) {
                        cnmea.dbt=atof(getf(3, nmeastr_p1)) + iconf.depth_transp;
                        cnmea.dbt_ts = ts;
                        continue;
                    }
                }

                // MTW - Water temperature in C
                if (NMPARSE(nmeastr_p1, "MTW")) {
                    cnmea.mtw=atof(getf(1, nmeastr_p1));
                    cnmea.mtw_ts = ts;
                    continue;
                }

                // MWV - Wind Speed and Angle (report VWR style)
                if (NMPARSE(nmeastr_p1, "MWV")) {
                    if (strncmp(getf(2, nmeastr_p1),"R",1) + strncmp(getf(4, nmeastr_p1),"N",1) == 0) {
                        cnmea.vwra=atof(getf(1, nmeastr_p1));
                        cnmea.vwrs=atof(getf(3, nmeastr_p1))/1.94; // kn 2 m/s;
                        if (cnmea.vwra > 180) {
                            cnmea.vwrd = 1;
                            cnmea.vwra = 360 - cnmea.vwra;
                        } else cnmea.vwrd = 0;
                        cnmea.vwr_ts = ts;
                    }  
                    if (strncmp(getf(2, nmeastr_p1),"T",1) + strncmp(getf(4, nmeastr_p1),"N",1) == 0) {
                        cnmea.vwta=atof(getf(1, nmeastr_p1));
                        cnmea.vwts=atof(getf(3, nmeastr_p1))/1.94; // kn 2 m/s;
                        cnmea.vwt_ts = ts;
                    } else if (ts - cnmea.stw_ts < INVALID && cnmea.stw > 0.9) {
                            cnmea.vwta=trueWindDirection(cnmea.rmc, cnmea.vwrs,  cnmea.vwra);
                            cnmea.vwts=trueWindSpeed(cnmea.rmc, cnmea.vwrs, cnmea.vwra);
                            cnmea.vwt_ts = ts;
                    }              
                    continue;
                }

                // VWR - Relative Wind Speed and Angle (obsoleted)
                if (ts - cnmea.vwr_ts > INVALID/2) { // If not from MWV
                    if (NMPARSE(nmeastr_p1, "VWR")) {
                        cnmea.vwra=atof(getf(1, nmeastr_p1));
                        cnmea.vwrs=atof(getf(3, nmeastr_p1))/1.94; // kn 2 m/s
                        cnmea.vwrd=strncmp(getf(2, nmeastr_p1),"R",1)==0? 0:1;
                        cnmea.vwr_ts = ts;
                        if (ts - cnmea.stw_ts < INVALID && cnmea.stw > 0.9) {
                            cnmea.vwta=trueWindDirection(cnmea.rmc, cnmea.vwrs,  cnmea.vwra);
                            cnmea.vwts=trueWindSpeed(cnmea.rmc, cnmea.vwrs, cnmea.vwra);
                            cnmea.vwt_ts = ts;
                        }
                        continue;
                    }
                }

                // SRT - Get AIS transceiver status on/off.
                if (NMPARSE(nmeastr_p1, "RT")) {
                    if (!strncmp("TXS", getf(1, nmeastr_p1),3)) {
                        if (atoi(getf(2, nmeastr_p1)))
                            cnmea.txs = 0;
                        else
                            cnmea.txs = 1;

                        cnmea.txs_ts = ts;
                    }
                    continue;
                }
            }

            // AIS VDM/VDO section
            if (!(is_vdm + is_vdo) || !aisconf.my_useais) continue;

            char p0b[sizeof(nmeastr_p1)];    // Only for debug

            if (ais_rval == 0)    // Clear the structure if we are done with the message(s).
                memset(&ais, 0 ,sizeof(ais_state));

            // Handle re-assembly and extraction of the 6-bit data from AIVDM/AIVDO sentences.
            ais_rval = assemble_vdm(&ais, nmeastr_p1);

            if (ais_rval) { // Multipart message (1) or error, get netxt!
                if (ais_rval > 1) ais_rval = 0;
                if (debug && ais_rval == 1) {
                    strcpy(p0b,nmeastr_p1); 
                }
                continue;
            }

            // Get the 6 bit message id
            ais.msgid = (unsigned char)get_6bit( &ais.six_state, 6 ); 

            // process message with appropriate parser
            switch( ais.msgid ) {
                case 1: // Message 1,2,3 - Position Report
                   if ( parse_ais_1( &ais, &msg_1 ) == 0 ) {
                        if ((userid = msg_1.userid)) {
                            pos2ddd( msg_1.latitude, msg_1.longitude, &lat_dd, &long_ddd );
                            if (msg_1.true != 511)
                                trueh = msg_1.true;
                            sog =  msg_1.sog;
                            cog = msg_1.cog;
                        }
                    }
                    break;

                case 2:
                   if ( parse_ais_2( &ais, &msg_2 ) == 0 ) {
                        if ((userid = msg_2.userid)) {
                            pos2ddd( msg_2.latitude, msg_2.longitude, &lat_dd, &long_ddd );
                            sog =  msg_2.sog;
                            cog = msg_2.cog;
                        }
                    }
                    break;

                case 3:
                   if ( parse_ais_3( &ais, &msg_3 ) == 0 ) {
                        if ((userid = msg_3.userid)) {
                            pos2ddd( msg_3.latitude, msg_3.longitude, &lat_dd, &long_ddd );
                            trueh = msg_3.true;
                            sog =  msg_3.sog;
                            cog = msg_3.cog;
                        }
                    }
                    break;

                case 4: // Message 4 - Base station report
                   if( r_limit == 10 && parse_ais_4( &ais, &msg_4 ) == 0 ) {
                        if ((userid = msg_4.userid)) {
                            pos2ddd( msg_4.latitude, msg_4.longitude, &lat_dd, &long_ddd );
                            aisname = "BASE STATION";
                        }
                    }
                    break;

                case 5: // Message 5: Ship static and voyage related data  
                   if ( parse_ais_5( &ais, &msg_5 ) == 0 ) {
                        if ((userid = msg_5.userid)) {
                            aisname = msg_5.name;
                        }
                    }
                    break;


                case 18: // Message 18 - Class B Position Report 
                   if ( parse_ais_18( &ais, &msg_18 ) == 0 ) {
                        if((userid = msg_18.userid)) {
                            pos2ddd( msg_18.latitude, msg_18.longitude, &lat_dd, &long_ddd );
                            sog = msg_18.sog;
                            cog = msg_18.cog;
                            if (!my_userid && is_vdo) {
                                my_userid = aisconf.my_userid = userid; // Our userid (if the transceiver is on)
                            }
                        }
                    }
                    break;

                case 19: // Message 19 - Extended Class B equipment position report 
                   if ( parse_ais_19( &ais, &msg_19 ) == 0 ) {
                        if((userid = msg_19.userid)) {
                            pos2ddd( msg_19.latitude, msg_19.longitude, &lat_dd, &long_ddd );
                            sog =  msg_19.sog;
                            aisname = msg_19.name;
                        }
                    }
                    break;

                case 21: // Message 21 - Aids To Navigation Report 
                   if ( parse_ais_21( &ais, &msg_21 ) == 0 ) { 
                        if((userid = msg_21.userid)) {
                            aisname = msg_21.name;
                        }
                    }
                    break;

                case 24: // Message 24 - Class B/CS Static Data Report 
                   if ( parse_ais_24( &ais, &msg_24 ) == 0 ) {      
                        if (msg_24.flags & 1) {
                            if((userid = msg_24.userid)) {
                                // Message 24 is a 2 part message. The first part only contains the MMSI
                                // and the ship name. The second part gives us the callsign.
                                if(msg_24.part_number == 0) 
                                    aisname = msg_24.name;

                                if(!strlen(my_aisname) && is_vdo && my_userid && msg_24.part_number == 0)
                                    strcpy(my_aisname, msg_24.name); // This is us (if the transceiver is on)

                                if ((msg_24.flags & 2) && msg_24.part_number == 1) {
                                    callsign = msg_24.callsign;
                                    if (!strlen(my_callsign) && is_vdo && my_userid) {
                                        strcpy(my_callsign, callsign); // Our callsign (if the transceiver is on)
                                    }
                                }
                                if (msg_24.part_number == 1)
                                    memset( &msg_24, 0, sizeof( aismsg_24 ));
					        }
                        }
                    }
                    break;

                default:
                    if(debug)
                        printf("not handled msgid = %d --- %s\n", ais.msgid, nmeastr_p1);
                    continue; break;

            }

            if (!userid)
                continue;

             if (!trueh && cog/10)
                trueh = cog/10;

            if (debug) {
                if (strlen(p0b)) {
                    printlog( "%s", p0b);
                    p0b[0] = '\0';
                }
                printlog( "%s", nmeastr_p1);
                printlog( "MESSAGE ID   : %d", ais.msgid );
                printlog( "NAME         : %s", aisname );
                printlog( "USER ID      : %ld", userid );
                printlog( "POSITION     : %0.6f %0.6f", fabs(lat_dd), fabs(long_ddd) );
                printlog( "TRUE HEADING : %ld", trueh );
                printlog( "COG HEADING  : %ld", cog/10 );
                printlog( "SOG          : %0.1f", sog/10 );
            }

            if (userid != aisconf.my_userid) {
                sog = sog == 1023? 0 : sog;

                // Add ship to the SQL RAM database
                (void)addShip(ais.msgid, userid, fabs(lat_dd), fabs(long_ddd), trueh, sog/10, aisname, aisconf.my_buddy);
                aisconf.my_buddy = 0;
            }

            if (++r_limit > 10) r_limit = 0;

            // Update GUI with this vessels' data (only if VDO and only once per session)
            if (got_vdo == 0 && fileFeed == 0 && strlen(my_aisname) && strlen(my_callsign)) {
                char vdobuf[200];
                sqlite3 *conn;
                sqlite3_stmt *res;
                const char *tail;

                for (int i=0; i < strlen(my_aisname); i++) {
                    if (my_aisname[i] == '@') {
                        my_aisname[i] = '\0';
                        break;
                    }
                }

                for (int i=0; i < strlen(my_callsign); i++) {
                    if (my_callsign[i] == '@') {
                        my_callsign[i] = '\0';
                        break;
                    }
                }

                sprintf(vdobuf, "UPDATE ais SET aisname = '%s', aiscallsign = '%s', aisid = %ld, ro = 1", my_aisname, my_callsign, aisconf.my_userid);
                printlog("Got our VDO from transceiver: MMSI= %ld, NAME= %s, CALLSIGN= %s", aisconf.my_userid, my_aisname, my_callsign);
                got_vdo = 1;
            
                (void)sqlite3_open_v2(NAVIDBPATH, &conn, SQLITE_OPEN_READWRITE, 0);
                if (conn == NULL) {
                        printlog("Failed to open database %s: ", (char*)sqlite3_errmsg(conn));                    
                } else {
                    if (sqlite3_prepare_v2(conn, vdobuf, -1,  &res, &tail)  == SQLITE_OK) {
                        if (sqlite3_step(res) != SQLITE_DONE) {
                            printlog("Failed to update row AIS: %s", (char*)sqlite3_errmsg(conn));
                        }
                        sqlite3_finalize(res);
                        sqlite3_close(conn);
                    }
                }           
            }
        }
    }

    exit_clean(SIGSTOP);

    return 0;
}

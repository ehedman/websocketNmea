/*
 * wsocknmea.c
 *
 *  Copyright (C) 2013-2018 by Erland Hedman <erland@hedmanshome.se>
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

#define FIFOKPLEX   "/tmp/fifo_kplex"       // KPlex fifo for file nmea input
#define FIFOPNMEA   "/tmp/fifo_nmea_p"      // KPlex fifo for "$P". These extended messages are not standardized. 

#define WSREBOOT    "/tmp/wss-reboot"           // Arguments to this server from WEB GUI

#ifndef UID         // UID/GID - Set to webserver's properties so that db and kplex config files can be writtin to
#define UID 33
#endif
#ifndef GID
#define GID 33
#endif

#ifdef REV
#define SWREV REV
#else
#define SWREV __DATE__
#endif

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
    SensorRelayStatus   = 204,
    WaterMakerData      = 210,
    ServerPing          = 900,
    TimeOfDAy           = 901,
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
static useconds_t lineRate = 1;
static char recFile[250];
static int fileFeed = 0;
static struct sockaddr_in peer_sa;
static struct lws_context *ws_context = NULL;
static int socketType = 0;
static char interFace[40];
static int socketCast = 0;
static char *programName = "wsocknmea";
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
    int     map_zoom;       // Google Map Zoom factor;
    int     map_updt;       // Update Time
    int     depth_vwrn;     // Depth visual low warning
    float   depth_transp;   // Depth of transponer
    char    adc_dev[40];    // ADC in /dev
} in_configs;

static in_configs iconf;

typedef struct {
    long    my_userid;  // AIS own user i.d
    long    my_buddy;   // AIS new buddy
    char    my_name[80];// AIS own name
    int     my_useais;  // AIS use or not
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
        strncpy(oldbuf,buf, sizeof(buf));
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
    int ad2Tick;
    static int ccnt;
    static float avcurr;
    static float sampcurr[20];  // No of samples to collect
    static int tcnt;
    static float avtemp;
    static float samptemp[20];
    static int vcnt;
    static float sampvolt[20];
    static float avvolt;

    ad2Tick = adcRead(voltChannel);
    a2dVal = tick2volt(ad2Tick); // Linearize this value and return voltage

    // Calculate an average in case of ADC drifts.
    if (a2dVal >= VOLTLOWLEVEL) {
        sampvolt[vcnt] = a2dVal;
        if (++vcnt > sizeof(sampvolt)/sizeof(float)) {
            vcnt = avvolt = 0;
            for (int i=0; i < sizeof(sampvolt)/sizeof(float); i++) {
                avvolt += sampvolt[i];
            }
            avvolt /= sizeof(sampvolt)/sizeof(float);
        }
        cn->volt = avvolt;
        cn->volt_ts = ts;
    }

    // Alert about low voltage
    a2dNotice(voltChannel, cn->volt, 11.5, 12.5);

    ad2Tick = adcRead(currChannel);
    a2dVal = tick2current(ad2Tick); // Linearize this value and return current

    // Calculate an average in case of ADC drifts.
    if (a2dVal >= CURRLOWLEVEL) {
        sampcurr[ccnt] = a2dVal;
        if (++ccnt > sizeof(sampcurr)/sizeof(float)) {
            ccnt = avcurr = 0;
            for (int i=0; i < sizeof(sampcurr)/sizeof(float); i++) {
                avcurr += sampcurr[i];
            }
            avcurr /= sizeof(sampcurr)/sizeof(float);
        }
        cn->curr = avcurr;
        cn->curr_ts = ts;
    }

    a2dVal = adcRead(tempChannel);
    a2dVal *= ADCTICKSTEMP;
    // Calculate an average in case of ADC drifts.
    if (a2dVal >= TEMPLOWLEVEL) {
        samptemp[tcnt] = a2dVal;
        if (++tcnt > sizeof(samptemp)/sizeof(float)) {
            tcnt = avtemp = 0;
            for (int i=0; i < sizeof(samptemp)/sizeof(float); i++) {
                avtemp += samptemp[i];
            }
            avtemp /= sizeof(samptemp)/sizeof(float);
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

void exit_clean(int sig)
{
    
    printlog("Terminating NMEA and KPLEX Services - reason: %d", sig);  
      
    fileFeed = 0;
    sigExit = 1;
    int fd;
    pid_t pid;
    char argstr[100];
    
    pNmeaStatus = 0;

    sleep(1);
   
    if (threadKplex) {
        if (pthread_self() != threadKplex)
            pthread_cancel(threadKplex);
    }
    
    if (pidKplex) {
        kill (pidKplex, SIGINT);
        pidKplex = 0;
    }
    
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
void wm_sock_open()
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
int  configure(int kpf)
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

                    sqlite3_prepare_v2(conn, "CREATE TABLE ais (Id INTEGER PRIMARY KEY, aisname TEXT, aisid BIGINT, aisuse INTEGER)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE abuddies (Id INTEGER PRIMARY KEY, userid BIGINT)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "CREATE TABLE devadc(Id INTEGER PRIMARY KEY, device TEXT, relay1txt TEXT, relay2txt TEXT, relay3txt TEXT, relay4txt TEXT)", -1, &res, &tail);
                    sqlite3_step(res);
                    
                    sprintf(buf, "INSERT INTO devadc (device) VALUES ('%s')", "/dev/null");
                    sqlite3_prepare_v2(conn, buf, -1, &res, &tail);
                    sqlite3_step(res);
#ifdef REV
                    sprintf(buf, "INSERT INTO rev (rev) VALUES ('%s')", REV);
#else
                    sprintf(buf, "INSERT INTO rev (rev) VALUES ('unknown')");
#endif
                    sqlite3_prepare_v2(conn, buf, -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO gmap (zoom,updt) VALUES (14,6)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO depth (vwrn,tdb) VALUES (4,1)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO sumlog (display,cal) VALUES (1,2)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO ais (aisname,aisid,aisuse) VALUES ('my yacht',366881180,1)", -1, &res, &tail);
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

    // Still in file feed config mode?
    if ((fd=open(KPCONFPATH, O_RDONLY)) > 0) {    
        if (read(fd, buf, sizeof(buf)) >0 && strstr(buf, FIFOKPLEX) != NULL) {
            rval = sqlite3_prepare_v2(conn, "select fname,rate from file", -1, &res, &tail);        
            if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                    (void)strcpy(recFile, (char*)sqlite3_column_text(res, 0));
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

void handle_buddy(long userid)
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

#define MAX_LONGITUDE 180
#define MAX_LATITUDE   90

float dms2dd(float coordinates, const char *val)
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
   // Explanations at: http://www.mapwindow.org/phorum/read.php?3,16271,16310*/
 
   b = coordinates/100;
   c= (coordinates/100 - b)*100 ;
   c /= 60;
   c += b;
   
   return c;
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


int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
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
                    if (ct - cnmea.rmc_ts > INVALID || cnmea.rmc == 0)
                        sprintf(value, "Exp-%d", req);
                    else
                        sprintf(value, "{'speedog':'%.2f'}-%d",cnmea.rmc, req);
                    break;
                }
               
                case SpeedThroughWater: {
                    if (ct - cnmea.stw_ts > INVALID || cnmea.stw == 0)
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
                            handle_buddy(atol(args));
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
#ifdef UK1104
                case SensorRelay: {
                    if (args != NULL && strlen(args)) {
                        relaySet(atoi(args));
                        sprintf(value, "{'relaySet':'%d'}-%d", relayStatus(), req);
                    }
                    break;
                }

                case SensorRelayStatus: {
                    sprintf(value, "{'relaySts':'%d'}-%d", relayStatus(), req);
                    break;
                }
#else
                case SensorRelay:
                case SensorRelayStatus: {
                    sprintf(value, "{'relaySts':'%d'}-%d", 0, req);
                    break;
                }          
#endif
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

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}

// "$P". These extended messages are not standardized. 
// Custom NMEA(P) going out to kplex fifo
void *threadPnmea_run()
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
    long long prevmsTs = 0;

    char fifobuf[60];
    char outbuf[70];

     if ((pnmeafd = open(FIFOPNMEA, O_RDWR)) < 0) {
        pNmeaStatus = 0;
        printlog("Error open fifo for p-type data: %s", strerror(errno));
    }

    msStartTime = current_timestamp();
    cnmea.startTime = msStartTime / 1000;

    cnmea.kWhp = cnmea.kWhn = 0;

    while (pNmeaStatus == 1)
    {
        mSts = current_timestamp();     // Get a timestamp for this turn
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
                } else downP += mSts - prevmsTs;
            } else {
                if (volt*fabs(curr) > 0.2) {
                    npkWn = (pkWhn * (samplesn-1) + volt * fabs(curr)) / samplesn;
                    pkWhn = npkWn;
                    cnmea.kWhn = ((npkWn * (msUpTime - downN)) / 3600000) / 1000;
                    samplesn++;
                } else downN += mSts - prevmsTs;
            }

            // Format: GPENV,volt,bank,current,bank,temp,where,kWhp,kWhn,startTimr*cs
            sprintf(fifobuf, "$GPENV,%.1f,1,%.1f,1,%.1f,1,%.3f,%.3f,%lu", cnmea.volt, cnmea.curr, cnmea.temp, cnmea.kWhp, cnmea.kWhn, cnmea.startTime);

            while(fifobuf[i] != '\0')
                checksum = checksum ^ fifobuf[i++];

            sprintf(outbuf,"%s*%x\r\n", fifobuf, checksum);
            write(pnmeafd, outbuf, strlen(outbuf));
        }
        prevmsTs = current_timestamp();
        sleep(1);
    }

    if (pnmeafd > 0)
        close(pnmeafd);

    pthread_exit(&rv);
}

// Make KPlex a persistent child of us
void *threadKplex_run()
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
void *t_fileFeed()
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

    if ((fdi=fopen(recFile,"r")) == NULL) {
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
    char txtbuf[300];
    int opts = 0;
    int c;
    int kplex_fork = 1;
    int detachstate;
    pid_t pid;
    pthread_attr_t attr;  
    pthread_t t1;
    struct sigaction action;
    struct lws_context_creation_info info;
    const char *cert_path = NULL;
    const char *key_path = NULL;
    pthread_t threadPnmea = 0;

    // Defaults
    wsport = WSPORT;
    memset(&peer_sa, 0, sizeof(struct sockaddr_in));
    memset(interFace, 0 ,sizeof(interFace));
    peer_sa.sin_family = AF_INET;
    
    recFile[0] = '\0';
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
                (void)strcpy(recFile, optarg);
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

    if (strlen(recFile) && !kplex_fork) {
        fprintf(stderr, "Error: incompatible options -n and -f\n");
        exit(EXIT_FAILURE);
    }

    if (kplex_fork) {
        // Make sure to restart kplex in case of config change
        FILE * cmd = popen("pidof kplex", "r");
        *txtbuf = '\0';
        if (fgets(txtbuf, 20, cmd) != NULL) {
            pid = strtoul(txtbuf, NULL, 10);
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
    if (strlen(recFile)) {  
        fileFeed = 1;
        pthread_create(&t1, &attr, t_fileFeed, NULL);         
    }

    if (access( FIFOPNMEA, F_OK ) == -1) {
        if (mkfifo(FIFOPNMEA, (mode_t)0664)) {
            printlog("Error create kplex p-type fifo: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (access( FIFOPNMEA, F_OK ) == 0) {      
        pthread_create(&threadPnmea, &attr, threadPnmea_run, NULL);
        if (!threadPnmea) {
            printlog("Failed to start threadPnmea thread");
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

#ifdef MT1800
    wm_sock_open();
#endif

#ifdef DOADC
    if (strcmp(iconf.adc_dev, "/dev/null")) {
        (void)adcInit(iconf.adc_dev, voltChannel);
        (void)adcInit(iconf.adc_dev, currChannel);
        (void)adcInit(iconf.adc_dev, tempChannel);
#ifdef UK1104
        (void)relayInit(4);      
#endif
    }
#endif
    
    sleep(3);
    memset(&cnmea, 0, sizeof(cnmea));
    aisconf.my_buddy = 0;
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

        ais_state ais;
        int ais_rval;
        long   userid;
        char *name;
        static int r_limit;
        char txtbuf_p1[200];
        static unsigned char ais_msgid_p1;
        int trueh;
        static int cog;

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
        name = NULL;
        userid = trueh = 0;
       
        // Reboot/re-configure request from PHP Gui code
        if (stat(WSREBOOT, &sb) == 0)
            break;

        // libwebsocket_service will process all waiting events with their
        // callback functions and then wait X ms.   
        lws_service(ws_context, POLLRATE);
       
        ts = time(NULL);        // Get a timestamp for this turn
       
        do_sensors(ts, &cnmea); // Do non NMEA things
  
        // Get the next sentence from the mux and update
        // the struct of valid data for the duration of 'INVALID'

        memset(txtbuf, 0, sizeof(txtbuf));
        
        // Clear out the ais structures if done with the message
        if (!ais_msgid_p1) memset( &ais, 0, sizeof(ais_state));      

        if (socketType == SOCK_STREAM) {
            cnt = recv(muxFd, txtbuf, sizeof(txtbuf), 0);
        } else {
            cnt = recvfrom(muxFd, txtbuf, sizeof(txtbuf), 0,(struct sockaddr *) &peer_sa, &socklen);
        }
       
        if (cnt < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            printlog("Error rerading NMEA-socket: %s", strerror(errno));
            sleep(2);
            continue;
        } else if (cnt > 0) {

            if (NMPARSE(txtbuf, "ENV")) { // We are talking to our self. Skip it
                continue;
            }

            cs = checksum = 0;     // Chekcsum portion at end of nmea string 
            for (i = 0; i < cnt; i++) {
                if (txtbuf[i] == '*') cs=i+1;
                if (txtbuf[i] == '\r' || txtbuf[i] == '\n') { txtbuf[i] = '\0'; break; }
            }

            if (cs > 0) {
                for (i=0; i < cs-1; i++) {
                    if (txtbuf[i] == '$' || txtbuf[i] == '!') continue;
                    checksum ^= (uint8_t)txtbuf[i];
                }
            }            
            
            if ( !cs || (checksum != (uint8_t)strtol(&txtbuf[cs], NULL, 16))) {
                if (debug) {
                    printlog("Checksum error in nmea sentence: 0x%02x/0x%02x - '%s'/'%s', pos %d", \
                        checksum, (uint8_t)strtol(&txtbuf[cs], NULL, 16), txtbuf, &txtbuf[cs], cs);
                }
                continue;
            }

            if (debug) fprintf( stderr, "Got '%s' as a mux message. Lenght = %d\n", txtbuf, (int)strlen(txtbuf));

            // Priority parsing order and logic:
            // See http://freenmea.net/docs and other sources out there
            
            // RMC - Recommended minimum specific GPS/Transit data
            // RMC feed is assumed to be present at all time 
            if (NMPARSE(txtbuf, "RMC")) {
                cnmea.rmc=atof(getf(7, txtbuf));
                cnmea.rmc_ts = ts;
                strcpy(cnmea.gll, getf(3, txtbuf));
                strcpy(cnmea.glo, getf(5, txtbuf));
                strcpy(cnmea.glns, getf(4, txtbuf));
                strcpy(cnmea.glne, getf(6, txtbuf));
                cnmea.gll_ts = ts;           
                continue;
            }

            // VTG - Track made good and ground speed
            if (ts - cnmea.rmc_ts > INVALID/2) { // If not from RMC
                if (NMPARSE(txtbuf, "VTG")) {
                    cnmea.rmc=atof(getf(5, txtbuf));
                    if ((cnmea.hdm=atof(getf(1, txtbuf))) != 0) // Track made good
                        cnmea.hdm_ts = ts;
                    continue;
                }
            }
    
            // GLL - Geographic Position, Latitude / Longitude
            if (ts - cnmea.gll_ts > INVALID/2) { // If not from RMC
                if (NMPARSE(txtbuf, "GLL")) {
                    strcpy(cnmea.gll, getf(1, txtbuf)); 
                    strcpy(cnmea.glo, getf(3, txtbuf));
                    strcpy(cnmea.glns, getf(2, txtbuf));
                    strcpy(cnmea.glne, getf(4, txtbuf));
                    cnmea.gll_ts = ts;
                    continue;
                }
            }

            // VHW - Water speed
            if(NMPARSE(txtbuf, "VHW")) {
                if ((cnmea.stw=atof(getf(5, txtbuf))) != 0)
                    cnmea.stw_ts = ts;
                continue;
            }

            if (ts - cnmea.stw_ts > INVALID && ts - cnmea.rmc_ts  <= INVALID) {
                cnmea.stw=cnmea.rmc;    // Not entirely correct but better than a blank instrument
                cnmea.stw_ts = ts;
            }

            if (ts - cnmea.hdm_ts > INVALID/2) { // If not from VHW

                // HDT - Heading - True
                if (NMPARSE(txtbuf, "HDT")) {
                    cnmea.hdm=atof(getf(1, txtbuf));
                    cnmea.hdm_ts = ts;
                    continue;
                }

                // HDG - Heading - Deviation and Variation 
                if (NMPARSE(txtbuf, "HDG")) {
                    cnmea.hdm=atof(getf(1, txtbuf));
                    cnmea.hdm_ts = ts;
                    continue;
                }
             
                // HDM Heading - Heading Magnetic
                if (NMPARSE(txtbuf, "HDM")) {
                    cnmea.hdm=atof(getf(1, txtbuf));
                    cnmea.hdm_ts = ts;
                    continue;
                }
            }

            // DPT - Depth (Depth of transponder added)
            if (NMPARSE(txtbuf, "DPT")) {
                cnmea.dbt=atof(getf(1, txtbuf))+atof(getf(2, txtbuf));
                cnmea.dbt_ts = ts;
                continue;
            }

            // DBT - Depth Below Transponder + GUI defined transponder depth
            if (ts - cnmea.dbt_ts > INVALID/2) { // If not from DPT
                if (NMPARSE(txtbuf, "DBT")) {
                    cnmea.dbt=atof(getf(3, txtbuf)) + iconf.depth_transp;
                    cnmea.dbt_ts = ts;
                    continue;
                }
            }

            // MTW - Water temperature in C
            if (NMPARSE(txtbuf, "MTW")) {
                cnmea.mtw=atof(getf(1, txtbuf));
                cnmea.mtw_ts = ts;
                continue;
            }

            // MWV - Wind Speed and Angle (report VWR style)
            if (NMPARSE(txtbuf, "MWV")) {
                if (strncmp(getf(2, txtbuf),"R",1) + strncmp(getf(4, txtbuf),"N",1) == 0) {
                    cnmea.vwra=atof(getf(1, txtbuf));
                    cnmea.vwrs=atof(getf(3, txtbuf))/1.94; // kn 2 m/s;
                    if (cnmea.vwra > 180) {
                        cnmea.vwrd = 1;
                        cnmea.vwra = 360 - cnmea.vwra;
                    } else cnmea.vwrd = 0;
                    cnmea.vwr_ts = ts;
                } else if (strncmp(getf(2, txtbuf),"T",1) + strncmp(getf(4, txtbuf),"N",1) == 0) {
                    cnmea.vwta=atof(getf(1, txtbuf));
                    cnmea.vwts=atof(getf(3, txtbuf))/1.94; // kn 2 m/s;
                    cnmea.vwt_ts = ts;
                }
                continue;
            }

            // VWR - Relative Wind Speed and Angle (obsolete)
            if (ts - cnmea.vwr_ts > INVALID/2) { // If not from MWV
                if (NMPARSE(txtbuf, "VWR")) {
                    cnmea.vwra=atof(getf(1, txtbuf));
                    cnmea.vwrs=atof(getf(3, txtbuf))/1.94; // kn 2 m/s
                    cnmea.vwrd=strncmp(getf(2, txtbuf),"R",1)==0? 0:1;
                    cnmea.vwr_ts = ts;
                    continue;
                }
            }

            if (*txtbuf != '!' || !aisconf.my_useais) continue;

            // AIS is handled last ....

            ais_rval = assemble_vdm(&ais, txtbuf);

            if (ais_rval == 1) { // We need to assemble part 2 of the message
                (void)strcpy(txtbuf_p1, txtbuf);
                ais_msgid_p1 = ais.msgid = (unsigned char)get_6bit( &ais.six_state, 6 );
                continue;
            } else if (ais_msgid_p1) {
                (void)strcat(txtbuf, txtbuf_p1);
                *txtbuf_p1 = '\0';
            }

            // Get the 6 bit message id
            if (!ais_msgid_p1)
                ais.msgid = (unsigned char)get_6bit( &ais.six_state, 6 );
            else { // Do with part 2
                ais.msgid = ais_msgid_p1;
                ais_msgid_p1 = 0;
            }
              
            // process message with appropriate parser
            switch( ais.msgid ) {
                case 1: // Message 1,2,3 - Position Report
                    if( !ais_rval && parse_ais_1( &ais, &msg_1 ) == 0 ) {
                        userid = msg_1.userid;
                        pos2ddd( msg_1.latitude, msg_1.longitude, &lat_dd, &long_ddd );
                        trueh = msg_1.true;
                        sog =  msg_1.sog;
                        cog = msg_1.cog;
                    }
                    break;

                case 2:
                    if( !ais_rval && parse_ais_2( &ais, &msg_2 ) == 0 ) {
                        userid = msg_2.userid;
                        pos2ddd( msg_2.latitude, msg_2.longitude, &lat_dd, &long_ddd );
                        trueh = msg_2.true;
                        sog =  msg_2.sog;
                        cog = msg_2.cog;
                    }
                    break;

                case 3:
                    if( !ais_rval && parse_ais_3( &ais, &msg_3 ) == 0 ) {
                        userid = msg_3.userid;
                        pos2ddd( msg_3.latitude, msg_3.longitude, &lat_dd, &long_ddd );
                        trueh = msg_3.true;
                        sog =  msg_3.sog;
                        cog = msg_2.cog;
                    }
                    break;

                case 4: // Message 4 - Base station report
                   if( r_limit == 10 && !ais_rval && parse_ais_4( &ais, &msg_4 ) == 0 ) {
                        userid = msg_4.userid;
                        pos2ddd( msg_4.latitude, msg_4.longitude, &lat_dd, &long_ddd );
                        name = "BASE STATION";
                    }
                    break;

                case 5: // Message 5: Ship static and voyage related data  
                    if( !ais_rval && parse_ais_5( &ais, &msg_5 ) == 0 ) {
                        userid = msg_5.userid;
                        name = msg_5.name;
                    }
                    break;

                case 18: // Message 18 - Class B Position Report 
                    if( !ais_rval && parse_ais_18( &ais, &msg_18 ) == 0 ) {
                        userid = msg_18.userid;
                        pos2ddd( msg_18.latitude, msg_18.longitude, &lat_dd, &long_ddd );
                        trueh = msg_18.true;
                        sog =  msg_18.sog;
                    }
                    break;

                case 19: // Message 19 - Extended Class B equipment position report 
                    if( !ais_rval && parse_ais_19( &ais, &msg_19 ) == 0 ) {
                        userid = msg_19.userid;
                        pos2ddd( msg_19.latitude, msg_19.longitude, &lat_dd, &long_ddd );
                        name = msg_19.name;
                        trueh = msg_19.true;
                        sog =  msg_19.sog;
                    }
                    break;

                case 21: // Message 21 - Aids To Navigation Report 
                    if( !ais_rval && parse_ais_21( &ais, &msg_21 ) == 0 ) { 
                        userid = msg_21.userid;                         
                        name = msg_21.name;
                    }
                    break;

                case 24: // Message 24 - Class B/CS Static Data Report - Part A 
                    if( !ais_rval && parse_ais_24( &ais, &msg_24 ) == 0 ) {      
                        if (msg_24.flags & 1) {
                            userid = msg_24.userid;
                            name = msg_24.name;
					    }                     
                    }
                    break;

                default: continue; break;

            }

            if (debug) {
                printlog( "MESSAGE ID   : %d", ais.msgid );
                printlog( "NAME         : %s", name );
                printlog( "USER ID      : %ld", userid );
                printlog( "POSITION     : %0.6f %0.6f", fabs(lat_dd), fabs(long_ddd));
                printlog( "TRUE HEADING : %ld", trueh);
                printlog( "SOG          : %0.1f", sog/10 );
            }
            if (!ais_rval && userid != aisconf.my_userid) {
                sog = sog == 1023? 0 : sog;
                if (trueh == 511) {
                    trueh = cog >= 3600? 360: cog/10; 
                }
                
                (void)addShip(ais.msgid, userid, fabs(lat_dd), fabs(long_ddd), trueh, sog/10, name, aisconf.my_buddy);
                aisconf.my_buddy = 0;
            }
            if (++r_limit > 10) r_limit = 0;
            if (debug && ais_rval) printlog("AIS return=%d, msgid=%d  msg='%s'\n", ais_rval, ais.msgid, txtbuf); 
       }
    }

    exit_clean(SIGSTOP);

    return 0;
}

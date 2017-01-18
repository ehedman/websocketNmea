/*
 * wsocknmea.c
 *
 *  Copyright (C) 2013-2017 by Erland Hedman <erland@hedmanshome.se>
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
#include <ctype.h>
#include <pthread.h>
#include <sqlite3.h>
#include <sys/mman.h>
#include <libwebsockets.h>
#include <syslog.h>

// Configuration
#define NMEAPORT 10110          // Port 10110 is designated by IANA for "NMEA-0183 Navigational Data"
#define NMEAADDR "127.0.0.1"    // localhost for TCP for UDP use 239.194.4.4 for a multicast address
#define WSPORT 9000             // Port for the websocket protocol (to be allowed by firewall)
#ifndef NAVIDBPATH
#define NAVIDBPATH  "/etc/default/navi.db"      // Configuration database writable for webserver
#endif
#ifndef KPCONFPATH
#define KPCONFPATH  "/etc/default/kplex.conf"   // KPlex configuration file writable for webserver
#endif
#define FIFOKPLEX   "/tmp/fifo_kplex"           // KPlex fifo for file input

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

#define MAX_TTYS    50  // No of serial devices to manage in the db
#define MAX_NICS    6   // No of nics to manage in the db

#define MT1800          // Instrument support for ENWA Watermaker 
#define DOADC           // Analog input volt .... etc.


#define POLLRATE    6   // Rate to collect data in ms.


#define INVALID     4   // Invalidate current sentences after # seconds without a refresh from talker.

#define NMPARSE(str, nsent) !strncmp(nsent, &str[3], strlen(nsent))

#ifdef DOADC
extern int adcInit(char *device, int a2dChannel);
extern int adcRead(int a2dChannel);
#define     ADCDEV          "/dev/spidev0.0"
#define     ADCTICKSVOLT    0.0065  // Must be adjusted to hw voltage divider resistance network etc.
#define     VOLTLOWLEVEL    1230    // No of adc ticks repesenting the threshold shown as the lowest level on the instrument.

enum adcChannels {
    voltChannel = 0,
    currChannel = 1
};
#endif

// Request codes from virtual instruments
enum requests {
    SpeedOverGround     = 100,
    SpeedThroughWater   = 101,
    DepthAndTemp        = 110,
    CompassHeading      = 120,
    GPS                 = 121,
    WindSpeedAndAngle   = 130,
    GoogleMapFeed       = 140,
    SensorVolt          = 200,
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
static int sigExit = 0;
static useconds_t lineRate = 1;
static char recFile[250];
static int fileFeed = 0;
static struct sockaddr_in peer_sa;
static struct sockaddr_in wmpeer_sa;
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
#endif

typedef struct {
    // Static configs from GUI sql db
    int     map_zoom;       // Google Map Zoom factor;
    int     map_updt;       // Update Time
    int     depth_vwrn;     // Depth visual low warning
    int     depth_swrn;     // Audiable low warning
    float   depth_transp;   // Depth of transponer
} in_configs;

static in_configs iconf;

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
    float   volt;       // Sensor Volt (non NMEA)
    time_t  volt_ts;    // Volt Timestamp
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
    int a2dVal;
    static int acnt;
    static int avvolt;
    static float aadc[10]; // No of samples to collect

    a2dVal = adcRead(voltChannel);
    // Calculate an average in case of ADC drifts.
    if (a2dVal >= VOLTLOWLEVEL) {  // example: 1230 ticks == 8 volt
        aadc[acnt] = (a2dVal);
        if (++acnt > sizeof(aadc)/sizeof(float)) {
            acnt = avvolt = 0;
            for (int i=0; i < sizeof(aadc)/sizeof(float); i++) {
                avvolt += aadc[i];
            }
            avvolt /= sizeof(aadc)/sizeof(float);
        }
        cn->volt = avvolt * ADCTICKSVOLT;
        cn->volt_ts = ts;
    }

#else
    // Just for demo
    cn->volt = (float)13.0 + (float)(rand() % 18)/100;
    cn->volt_ts = ts;
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
    int fd, cnt;
    pid_t pid;
    char argstr[100];
    
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
            if ((cnt=read(fd, argstr, sizeof(argstr)))>0) {
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

    exit(sig);
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
    long flags;
    struct ip_mreq mreq;

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
                char * str = "[tcp]\nmode=server\naddress=127.0.0.1\nport=10110\n";
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

                    sqlite3_prepare_v2(conn, "CREATE TABLE gmap(Id INTEGER PRIMARY KEY, zoom INTEGER, updt INTEGER)", -1, &res, &tail);
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

                    sqlite3_prepare_v2(conn, "INSERT INTO gmap (zoom,updt) VALUES (14,6)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO depth (vwrn,tdb) VALUES (4,1)", -1, &res, &tail);
                    sqlite3_step(res);

                    sqlite3_prepare_v2(conn, "INSERT INTO sumlog (display,cal) VALUES (1,2)", -1, &res, &tail);
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

    // Still in file feed config mode?
    if ((fd=open(KPCONFPATH, O_RDONLY)) > 0) {
        char buf[100];      
        if (read(fd, buf, sizeof(buf)) >0 && strstr(buf, FIFOKPLEX) != NULL) {
            rval = sqlite3_prepare_v2(conn, "select fname,rate from file", -1, &res, &tail);        
            if (rval == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
                    (void)strcpy(recFile, (char*)sqlite3_column_text(res, 0));
                    lineRate = sqlite3_column_int(res, 1);
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

int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len)
{
    return 0;
}

// Handle all requests from remote virtual instruments.

static int callback_nmea_parser(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len)
{
    int  i, cnt, req, rval = 0;
    char value [200];
    unsigned char *buf;
    memset(value, 0, sizeof(value));
    time_t ct = time(NULL);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
#if 0
            printlog("Connection established");
#endif
            break;

        case LWS_CALLBACK_RECEIVE: {
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
                        sprintf(value, "{'la':'%f','lo':'%f','N':'%s','E':'%s','A':'%.0f','zoom':'%d','updt':'%d'}-%d", \
                            dms2dd(atof(cnmea.gll),"m"),dms2dd(atof(cnmea.glo),"m"), \
                            cnmea.glns,cnmea.glne,cnmea.hdm,iconf.map_zoom,iconf.map_updt,req);
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

           if (!(rval = strlen(value))) return 0;
           
           for (i=0; i<rval; i++) // Format valid JSON
                if (value[i] == '\'') value[i] = '"';

           // Create a buffer to hold our response.
           // It has to have some pre and post padding as demanded by the websocket protocol.
           buf = (unsigned char*) malloc(LWS_SEND_BUFFER_PRE_PADDING + sizeof(value) +
                                                       LWS_SEND_BUFFER_POST_PADDING);
          
           sprintf((char*)&buf[LWS_SEND_BUFFER_PRE_PADDING],"%s", value);
          
          
           // Log what we recieved and what we're going to send as a response.
           if (debug) {
                printlog("received command: %s, replying: %.*s", (char *) in, (int) rval, buf + LWS_SEND_BUFFER_PRE_PADDING);
           }
          
           // Send response.
           // Notice that we have to tell where exactly our response starts. That's
           // why there's `buf[LWS_SEND_BUFFER_PRE_PADDING]` and how long it is.
           cnt = lws_write(wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], rval, LWS_WRITE_TEXT);
          
           free(buf);

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
       "http-only",     // name
       callback_http,   // callback
       0                // per_session_data_size
    },
    {
       "nmea-parser-protocol",  // protocol name - very important!
       callback_nmea_parser,    // callback
       0                        // we don't use any per session data
    },
    {
       NULL, NULL, 0   /* End of list */
    }
};

// Make KPlex a persistent child of us
void *threadKplex_run()
{
    time_t stt = 0;
    int retry = 3;
    static int rv;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
 
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
    
    if ((fdi=fopen(recFile,"r")) == NULL) {
        printlog("Error open feed file: %s", strerror(errno));
        pthread_exit(&rval);
    }
    
    if (access( FIFOKPLEX, F_OK ) == -1) {
        if (mkfifo(FIFOKPLEX, (mode_t)0664)) {
            printlog("Error create kplex fifo: %s", strerror(errno));
            pthread_exit(&rval);
        }
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
    char txtbuf[200];
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
    (void)adcInit(ADCDEV, voltChannel);
#endif
    
    sleep(3);
    memset(&cnmea, 0, sizeof(cnmea));

    // Main loop, to end this server, send signal INT(^c) or TERM
    // GUI commands WSREBOOT can break this loop requesting a restart.
    // Keep the collected_nmea struct up to date with collected data

    while(1) {

        int i, cnt, cs;
        time_t ts;
        unsigned char checksum;
        socklen_t socklen = sizeof(peer_sa);
        struct stat sb;

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
        
        cs = 0;     // Chekcsum portion at end of nmea string 

        if (socketType == SOCK_STREAM) {
            cnt = recv(muxFd, txtbuf, sizeof(txtbuf), 0);
        } else {
            cnt = recvfrom(muxFd, txtbuf, sizeof(txtbuf), 0,(struct sockaddr *) &peer_sa, &socklen);
        }
       
        if (cnt < 0) {
            if (errno == EAGAIN) {usleep(3000); continue;}
            printlog("Error rerading NMEA-socket: %s", strerror(errno));
            sleep(2);
        } else if (cnt > 0) {

            for (i = 0; i < cnt; i++) {
                if (txtbuf[i] == '*') cs=i+1;
                if (txtbuf[i] == '\r' || txtbuf[i] == '\n') txtbuf[i] = '\0';
            }
            if (debug) fprintf( stderr, "Got '%s' as a kplex message. Lenght = %d\n", txtbuf, (int)strlen(txtbuf));
        
            // AIS throw 
            if (!strncmp("AIVDM",&txtbuf[1],5)) continue;
            if (!strncmp("DUAIQ",&txtbuf[1],5)) continue;

            checksum = 0;
            if (cs > 0) {
                for (i=0; i < cs-1; i++) {
                    if (txtbuf[i] == '$') continue;
                    checksum ^= (unsigned char)txtbuf[i];
                }
            }            
            
            if ( !cs || (checksum != strtol(&txtbuf[cs], NULL, 16))) {
                if (debug) {
                    printlog("Checksum error in nmea sentence: %02x/%02x - %s", checksum, (unsigned int)strtol(&txtbuf[cs], NULL, 16), txtbuf);
                }
                continue;
            }

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
                    cnmea.rmc_ts = ts;
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

            // VHW - Water speed and Heading
            if (NMPARSE(txtbuf, "VHW")) {
                cnmea.hdm=atof(getf(3, txtbuf));
                cnmea.hdm_ts = ts;
                cnmea.stw=atof(getf(5, txtbuf));
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
       }
    }

    exit_clean(SIGSTOP);

    return 0;
}

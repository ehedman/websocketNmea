#ifndef WSOCKNMEA_H
#define WSOCKNMEA_H

#ifndef NAVIDBPATH
#define NAVIDBPATH  "/etc/default/navi.db"      // Configuration database writable for webserver
#endif

int ignored __attribute__((unused));

extern void printlog(char *format, ...);

struct aisShip_struct
{
    char *js;
    struct aisShip_struct *next;
};

extern int addShip(int msgid, long userid, double lat_dd, double long_ddd, int trueh, double sog, char *name, long buddie);
extern struct aisShip_struct *getShips(int size);

//#define MT1800              // Instrument support for ENWA Watermaker 
//#define MCP3208             // Analog input valtage, current .... etc.
#define UK1104              // CanaKit 4-Channel USB Relay Board with 6-Channel A/D Interface
#define TPMCH   5           // To be identifyed as temp chanel

enum adcChannels {
    voltChannel = 0,
    currChannel,
    tempChannel = TPMCH,    // Reserved to return real temp as float value
};

extern int adcInit(char *device, int a2dChannel); // device exaple "//dev/ttyACM0" or "/dev/spidev0.0"
extern float adcRead(int a2dChannel);
extern void a2dNotice(int channel, float val, float low, float high);
extern float tick2volt(int tick);
extern float tick2current(int tick);

#ifdef UK1104

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
#define IOWAIT  350000  // Wait for non blocked data .. usec ..
#define MAXTRY  5       // .. and re-try .. before giving up on ser. data.
#define MAXAGE  20      // Invalidate after .. s

#define CHAisNOTUSED    0
#define CHAisCLAIMED    1
#define CHAisREADY      2

enum types {
    DigOut = 1,
    DigIn,
    AnaogIn,
    TempIn,
    Relay
};

extern void relayInit(int nchannels);
extern void relaySet(int channels);
extern int relayStatus(void);
extern int ioPinInit(int channel, int type);
extern void ioPinset(int channel, int mode);
extern int ioPinGet(int channel);

#endif // UK1104

#if defined (MCP3208) || defined (UK1104)
#define     VOLTLOWLEVEL    8.0     // Voltage repesenting the threshold shown as the lowest level on the instrument
#define     TEMPLOWLEVEL    -25.0   // Temperature repesenting the threshold shown as the lowest level on the instrument
#define     CURRLOWLEVEL    -30.0   // Current repesenting the threshold shown as the lowest level on the instrument
#ifdef UK1104
#define     ADCRESOLUTION   1023    // ADC Resolution (10bit)
#define     ADCTICKSVOLT    0.0486  // Must be adjusted to hw voltage divider resistance network etc.
#define     ADCTICKSCURR    0.02    // Must be adjusted to hw voltage divider resistance network etc.
#else   // MCP3208
#define     ADCRESOLUTION   4095    // ADC Resolution (12bit)
#define     ADCTICKSVOLT    0.0065  // Must be adjusted to hw voltage divider resistance network etc.
#define     ADCTICKSCURR    0.005   // Must be adjusted to hw voltage divider resistance network etc.
#define     ASCTICKSTEMP    0.008   // Must be adjusted to hw voltage divider resistance network etc.
#endif

#define MSGPRG   "/usr/local/bin/a2dnotice"
#define MSGVLOW  "%s \"Main battery bank at critical %.2f Volt\""
#define MSGVHIGH "%s \"Main battery bank back at %.2f Volt\""

typedef int linearizeSensor[100][2];

#endif

#endif /* WSOCKNMEA_H */

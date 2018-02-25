#ifndef WSOCKNMEA_H
#define WSOCKNMEA_H

#ifndef NAVIDBPATH
#define NAVIDBPATH  "/etc/default/navi.db"      // Configuration database writable for webserver
#endif

//#define MT1800              // Instrument support for ENWA Watermaker 
//#define MCP3208             // Analog input volt .... etc.
#define UK1104              // CanaKit 4-Channel USB Relay Board with 6-Channel A/D Interface
#define TPMCH   5           // To be identifyed as temp chanel with float type return (UK1104)

enum adcChannels {
    voltChannel = 0,
    currChannel,
#ifdef UK1104
    tempChannel = TPMCH,    // Reserved to return real temp as float value
#endif
};

extern int adcInit(char *device, int a2dChannel); // device exaple "//dev/ttyACM0" or "/dev/spidev0.0"
extern float adcRead(int a2dChannel);
extern void a2dNotice(int channel, float val, float low, float high);

#ifdef UK1104
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
#endif

#if defined (MCP3208) || defined (UK1104)
#define     TEMPLOWLEVEL    -25.0   // Max minus temp in C on instrument scale
#ifdef UK1104
#define     VOLTLOWLEVEL    8.0     // Voltage repesenting the threshold shown as the lowest level on the instrument
#define     ADCTICKSVOLT    1.0     // Not really used
#define     CURRLOWLEVEL    400
#define     ADCTICKSCURR    0.02
#else   // MCP3208
#define     ADCTICKSVOLT    0.0065  // Must be adjusted to hw voltage divider resistance network etc.
#define     VOLTLOWLEVEL    1230    // No of adc ticks repesenting the threshold shown as the lowest level on the instrument.
#define     CURRLOWLEVEL    1024
#define     ADCTICKSCURR    0.005
#endif
#endif

#define MSGPRG   "/usr/local/bin/a2dnotice"
#define MSGVLOW  "%s \"Main battery bank at critical %.2f Volt\""
#define MSGVHIGH "%s \"Main battery bank back at %.2f Volt\""

extern void printlog(char *format, ...);

struct aisShip_struct
{
    char *js;
    struct aisShip_struct *next;
};

extern int addShip(int msgid, long userid, double lat_dd, double long_ddd, int trueh, double sog, char *name, long buddie);
extern struct aisShip_struct *getShips(int size);
extern float tick2volt(int tick);

#endif /* WSOCKNMEA_H */

#ifndef WSOCKNMEA_H
#define WSOCKNMEA_H

#ifndef NAVIDBPATH
#define NAVIDBPATH  "/etc/default/navi.db"      // Configuration database writable for webserver
#endif

//#define MT1800              // Instrument support for ENWA Watermaker 
//#define MCP3208             // Analog input volt .... etc.
#define UK1104              // CanaKit 4-Channel USB Relay Board with 6-Channel A/D Interface
#define TPMCH   5           // To be identifyed as temp chanel with float type return (UK1104)

extern void printlog(char *format, ...);

struct aisShip_struct
{
    char *js;
    struct aisShip_struct *next;
};

extern int addShip(int msgid, long userid, double lat_dd, double long_ddd, int trueh, double sog, char *name, long buddie);
extern struct aisShip_struct *getShips(int size);

#endif /* WSOCKNMEA_H */

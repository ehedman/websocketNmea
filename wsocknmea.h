

#ifndef WSOCKNMEA_H
#define WSOCKNMEA_H

extern void printlog(char *format, ...);

#ifdef AIS
struct aisShip_struct
{
    char *js;
    struct aisShip_struct *next;
};

extern int addShip(int msgid, long userid, double lat_dd, double long_ddd, char *name);
extern struct aisShip_struct *getShips(int size);
#endif /* AIS */

#endif /* WSOCKNMEA_H */

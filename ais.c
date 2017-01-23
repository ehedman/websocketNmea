#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sqlite3.h>
#include "wsocknmea.h"

#ifdef AIS

#define MAX_INACTIVE    40      // Aging vessel signals disabled after # seconds
#define MAX_LIVE        300     // .. removed after # seconds

static int first = 1;

static sqlite3 *conn;
static const char *tail;

static struct aisShip_struct *head = NULL;

static int createDb(void)
{
    sqlite3_stmt *res;

    (void)sqlite3_open_v2(":memory:", &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE , 0);
    if (conn == NULL) {
        printlog("Failed to create a new AIS database %s: ", (char*)sqlite3_errmsg(conn));
        return 1;
    }
    printlog("AIS in-memory database created");

    if (sqlite3_prepare_v2(conn, "CREATE TABLE ais(Id INTEGER PRIMARY KEY, msgid INTEGER, userid BIGINT, lat_dd DOUBLE, long_ddd DOUBLE, name TEXT, ts BIGINT)", -1, &res, &tail) != SQLITE_OK) 
        printlog("sqlite3 create table: %s", (char*)sqlite3_errmsg(conn));
    else if (sqlite3_step(res) != SQLITE_DONE)
        printlog("sqlite3 create table step: %s", (char*)sqlite3_errmsg(conn));

    (void)sqlite3_finalize(res);

    return 0;

}

int addShip(int msgid, long userid, double lat_dd, double long_ddd, char *name)
{
    char buf[200];
    sqlite3_stmt *res, *res1;

    if (first) {

       if (createDb())
            return 1;

        first = 0;
    }
    
    if ((lat_dd + long_ddd) && userid) {

        (void)sprintf(buf, "SELECT userid FROM ais WHERE userid = '%ld'", userid);

        if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            (void)sprintf(buf, "UPDATE ais SET msgid = '%d', userid = '%ld', lat_dd = '%0.6f', long_ddd = '%0.6f', ts = '%ld' WHERE userid = '%ld'", \
            msgid, userid, lat_dd, long_ddd, time(NULL), userid);

            if (sqlite3_prepare_v2(conn, buf, -1, &res1, &tail) != SQLITE_OK)
                printlog("sqlite3 update: %s", (char*)sqlite3_errmsg(conn));
            else if (sqlite3_step(res) != SQLITE_DONE)
                printlog("sqlite3 update step: %s", (char*)sqlite3_errmsg(conn));

            (void)sqlite3_finalize(res1);
            (void)sqlite3_finalize(res);
            
            return 0;
        }
        (void)sqlite3_finalize(res);

        (void)sprintf(buf, "INSERT INTO ais (msgid,userid,lat_dd,long_ddd,ts,name) VALUES (%d,%ld,%0.6f,%0.6f,%ld,'unknown')", \
            msgid, userid, lat_dd, long_ddd, time(NULL));

        if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) != SQLITE_OK)
            printlog("sqlite3 insert: ", (char*)sqlite3_errmsg(conn));
        else if (sqlite3_step(res) != SQLITE_DONE)
            printlog("sqlite3 insert step: %s", (char*)sqlite3_errmsg(conn));

        (void)sqlite3_finalize(res);

    } else if (name != NULL && userid) {
        
        for (int i=0; i <strlen(name); i++) {
            if (name[i] == '@' || !isprint(name[i])) {
                name[i] = '\0';
                break;
            }
        }
        (void)sprintf(buf, "UPDATE ais SET name = '%s' WHERE userid = '%ld'", name, userid);

        if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) == SQLITE_OK)
            (void)sqlite3_step(res);         
        (void)sqlite3_finalize(res);    
    }

    (void)sprintf(buf, "DELETE from ais WHERE ts < '%ld'", time(NULL)-MAX_LIVE);

    if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 delete old: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
        return 0;
    }

    if (sqlite3_step(res) != SQLITE_DONE)
        printlog("sqlite3 delete: %s", (char*)sqlite3_errmsg(conn));

    (void)sqlite3_finalize(res);

    return 0;
}

struct aisShip_struct *getShips(int maxSize)
{
    char buf[200];
    char *jptr;
    struct aisShip_struct *ptr;
    sqlite3_stmt *res;
    size_t len;
    int j_size;

    if (first) return NULL;

    if (sqlite3_prepare_v2(conn, "SELECT COUNT('userid') from ais", -1, &res, &tail) == SQLITE_OK) {
        (void)sqlite3_step(res);
        int cnt = sqlite3_column_int(res, 0);
        (void)sqlite3_finalize(res);
        if (!cnt) return NULL;
    }

    (void)sprintf(buf, "DELETE from ais WHERE ts < '%ld'", time(NULL)-MAX_LIVE);

    if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 delete old: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
    } else if (sqlite3_step(res) != SQLITE_DONE)
        printlog("sqlite3 delete: %s", (char*)sqlite3_errmsg(conn));

    (void)sqlite3_finalize(res);

    (void)sprintf(buf, "SELECT * from ais WHERE ts < '%ld'", time(NULL)-MAX_INACTIVE);

    if (sqlite3_prepare_v2(conn, buf, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 select for row: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
        return NULL;
    }

    head = ptr = NULL;
    j_size = 0;   

    while (sqlite3_step(res) == SQLITE_ROW) {

        if (head == NULL) {
            head = ptr = (struct aisShip_struct*)malloc(sizeof(struct aisShip_struct));
            ptr->next = NULL;
        }    
           
        j_size += len = (size_t)sprintf(buf, "'msgid':'%d','userid':'%ld','la':'%0.6f','lo':'%0.6f','name':'%s'", \
                sqlite3_column_int(res, 1), (long)sqlite3_column_int(res, 2), \
                sqlite3_column_double(res, 3), sqlite3_column_double(res, 4), sqlite3_column_text(res, 5));

        if (j_size <= maxSize) {
            jptr=malloc(len+1);
            memset(jptr, 0, len+1);

            (void)strncpy(jptr, buf, len);

            if (ptr->next == NULL) {                
                ptr->js = jptr;
                ptr->next = ptr;
            } else {
                ptr->next = (struct aisShip_struct*)malloc(sizeof(struct aisShip_struct));
                ptr->next->js = jptr;
                ptr = ptr->next;
            }
        }          
    }

    (void)sqlite3_finalize(res);

    if (j_size)
        ptr->next = NULL;   // terminate list

    return head;
}
#endif /* AIS */



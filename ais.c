/*
 * ais.c
 *
 *  Copyright (C) 2013-2018 by Erland Hedman <erland@hedmanshome.se>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sqlite3.h>
#include "wsocknmea.h"

#define MAX_INACTIVE    120      // Aging vessel signals hidden after # seconds
#define MAX_LIVE        300     // .. removed after # seconds

static int first = 1;

static sqlite3 *conn = NULL;
static const char *tail;

static struct aisShip_struct *head = NULL;

static int createDb(void)
{
    sqlite3_stmt *res, *res1 = NULL;
    int rval = 0;
    static sqlite3 *conn1 = NULL;
    char sql[200];

    (void)sqlite3_open_v2(":memory:", &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE , 0);
    if (conn == NULL) {
        printlog("Failed to create a new AIS database %s: ", (char*)sqlite3_errmsg(conn));
        return 1;
    }


    if (sqlite3_prepare_v2(conn, "CREATE TABLE ais(Id INTEGER PRIMARY KEY, msgid INTEGER, userid BIGINT, lat_dd DOUBLE, long_ddd DOUBLE, sog DOUBLE, trueh INTEGER, name TEXT, ts BIGINT)", -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 create table: %s", (char*)sqlite3_errmsg(conn));
        rval = 1;
    } else if (sqlite3_step(res) != SQLITE_DONE) {
        printlog("sqlite3 create table step: %s", (char*)sqlite3_errmsg(conn));
        rval = 1;
    }

    (void)sqlite3_finalize(res);

    // Get our permanent buddies into a second table
    if (!rval && sqlite3_open_v2(NAVIDBPATH, &conn1, SQLITE_OPEN_READONLY, 0) == SQLITE_OK) {

        if (sqlite3_prepare_v2(conn, "CREATE TABLE buddies (Id INTEGER PRIMARY KEY, userid BIGINT)", -1, &res, &tail) == SQLITE_OK) {

            if (sqlite3_step(res) == SQLITE_DONE) {
                if (sqlite3_prepare_v2(conn1, "SELECT userid FROM abuddies", -1, &res1, &tail) == SQLITE_OK) {

                    while (sqlite3_step(res1) == SQLITE_ROW)
                    {
                        (void)sprintf(sql, "INSERT INTO buddies (userid) VALUES (%ld)", (long)sqlite3_column_int(res1, 0));
                        (void)sqlite3_prepare_v2(conn, sql, -1, &res, &tail);
                        (void)sqlite3_step(res);
                    }

                    (void)sqlite3_finalize(res1);
                } else { printlog("sqlite3 select from abuddies table: %s", (char*)sqlite3_errmsg(conn1)); (void)sqlite3_finalize(res1); }
            } else printlog("sqlite3 step buddy table: %s", (char*)sqlite3_errmsg(conn));
        } else printlog("sqlite3 create buddies table: %s", (char*)sqlite3_errmsg(conn));
    } else printlog("sqlite3 open navidb for buddy transfers: %s", (char*)sqlite3_errmsg(conn1));

    (void)sqlite3_finalize(res);
    if (conn1) {
        (void)sqlite3_close(conn1);
    }
  
    if (!rval)
        printlog("AIS in-memory database created");

    return rval;

}

int addShip(int msgid, long userid, double lat_dd, double long_ddd, int trueh, double sog, char *name, long buddy)
{
    char sql[200];
    sqlite3_stmt *res, *res1;
    int i;

    if (first) {

       if (createDb())
            return 1;

        first = 0;
    }
    
    if ((lat_dd + long_ddd) && userid) {

        (void)sprintf(sql, "SELECT userid FROM ais WHERE userid = %ld", userid);

        if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            if (trueh) {
                (void)sprintf(sql, "UPDATE ais SET msgid = '%d', userid = %ld, lat_dd = %0.6f, long_ddd = %0.6f, sog = %0.1f, trueh = %d, ts = %ld WHERE userid = %ld", \
                msgid, userid, lat_dd, long_ddd, sog, trueh, time(NULL), userid);
            } else {
                (void)sprintf(sql, "UPDATE ais SET msgid = '%d', userid = %ld, lat_dd = %0.6f, long_ddd = %0.6f, sog = %0.1f, ts = %ld WHERE userid = %ld", \
                msgid, userid, lat_dd, long_ddd, sog, time(NULL), userid);
            }

            if (sqlite3_prepare_v2(conn, sql, -1, &res1, &tail) != SQLITE_OK)
                printlog("sqlite3 update: %s", (char*)sqlite3_errmsg(conn));
            else if (sqlite3_step(res1) != SQLITE_DONE)
                printlog("sqlite3 update step: %s", (char*)sqlite3_errmsg(conn));

            (void)sqlite3_finalize(res1);
            (void)sqlite3_finalize(res);

        } else {
            (void)sqlite3_finalize(res);

            (void)sprintf(sql, "INSERT INTO ais (msgid,userid,lat_dd,long_ddd,sog,trueh,name,ts) VALUES (%d,%ld,%0.6f,%0.6f,%0.1f,%d,'n.n',%ld)", \
                msgid, userid, lat_dd, long_ddd, sog, trueh, time(NULL));

            if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) != SQLITE_OK)
                printlog("sqlite3 insert: %s", (char*)sqlite3_errmsg(conn));
            else if (sqlite3_step(res) != SQLITE_DONE)
                printlog("sqlite3 insert step: %s", (char*)sqlite3_errmsg(conn));

            (void)sqlite3_finalize(res);
        }
    }
    
    if (name != NULL && userid) { // msg_5, msg_24

        int upd = 0;
        int len;

        if (!(len = strlen(name)))
            return 0;

        (void)sprintf(sql, "SELECT COUNT('userid') FROM ais WHERE userid = '%ld'", userid);

        if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK) {
            (void)sqlite3_step(res);
            upd = sqlite3_column_int(res, 0);
            (void)sqlite3_finalize(res);
        }
        
        for (i=0; i <len; i++) {
            if (name[i] == '@' || !isprint(name[i])) {
                name[i] = '\0';
                break;
            }
        }
        while(--len) {
            if (name[len] == ' ')
                name[len] = '\0';
            else break;
        }
        
        if (upd) {
            (void)sprintf(sql, "UPDATE ais SET name = '%s', ts = %ld  WHERE userid = %ld", name, time(NULL), userid);
            if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK)
                (void)sqlite3_step(res);    
            (void)sqlite3_finalize(res); 
        }  else {
            (void)sprintf(sql, "INSERT INTO ais (lat_dd,long_ddd,userid,name,ts) VALUES (0,0,%ld,'%s',%ld)", userid, name, time(NULL));
            if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK)
                (void)sqlite3_step(res);   
            (void)sqlite3_finalize(res); 
        } 
    }

    if (buddy) { // buddy just any new to be registered in this call possibly unrelated to userid
        (void)sprintf(sql, "SELECT userid FROM buddies WHERE userid = %ld", buddy);
        if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK && sqlite3_step(res) == SQLITE_ROW) {
            (void)sqlite3_finalize(res);
            (void)sprintf(sql, "DELETE FROM buddies WHERE userid = %ld", buddy);
            if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) == SQLITE_OK)
                (void)sqlite3_step(res);
            (void)sqlite3_finalize(res);   
        } else {
            (void)sqlite3_finalize(res);
            (void)sprintf(sql, "INSERT INTO buddies (userid) VALUES (%ld)", buddy);  
            if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) ==  SQLITE_OK)
                (void)sqlite3_step(res);
            (void)sqlite3_finalize(res);       
        }
    }

    (void)sprintf(sql, "DELETE from ais WHERE ts < %ld", time(NULL)-MAX_LIVE);

    if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 delete old: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
        return 0;
    }

    if (sqlite3_step(res) != SQLITE_DONE)
        printlog("sqlite3 step delete old: %s", (char*)sqlite3_errmsg(conn));

    (void)sqlite3_finalize(res);

    return 0;
}

struct aisShip_struct *getShips(int maxSize)
{
    char sql[200];
    char json[200];
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

    (void)sprintf(sql, "DELETE from ais WHERE ts < %ld", time(NULL)-MAX_LIVE);

    if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 delete old: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
    } else if (sqlite3_step(res) != SQLITE_DONE)
        printlog("sqlite3 step delete old: %s", (char*)sqlite3_errmsg(conn));

    (void)sqlite3_finalize(res);

    (void)sprintf(sql, "SELECT B.userid, A.* FROM ais A LEFT OUTER JOIN [buddies] B USING (userid) WHERE A.lat_dd > 0.0 AND A.ts > %ld", time(NULL)-MAX_INACTIVE);
    
    if (sqlite3_prepare_v2(conn, sql, -1, &res, &tail) != SQLITE_OK) {
        printlog("sqlite3 select for row: %s", (char*)sqlite3_errmsg(conn));
        (void)sqlite3_finalize(res);
        return NULL;
    }

    head = ptr = NULL;
    j_size = 0;   

    while (sqlite3_step(res) == SQLITE_ROW) {

        if (head == NULL) {
            head = ptr = (struct aisShip_struct*)malloc(sizeof(struct aisShip_struct));
            if (head != NULL)
                ptr->next = NULL;
            else break;
        }    
           
        memset(json, 0, sizeof(json));

        j_size += len = (size_t)sprintf(json, "'buddyid':'%ld','msgid':'%d','userid':'%ld','la':'%0.6f','lo':'%0.6f','sog':'%0.1f','trueh':'%d','name':'%s'", \
                (long)sqlite3_column_int(res, 0),sqlite3_column_int(res, 2), (long)sqlite3_column_int(res, 3), sqlite3_column_double(res, 4), sqlite3_column_double(res, 5), \
                sqlite3_column_double(res, 6),sqlite3_column_int(res, 7), sqlite3_column_text(res, 8));

        if (j_size <= maxSize) {
            jptr=malloc(len+1);
            memset(jptr, 0, len+1);

            (void)strncpy(jptr, json, len);

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



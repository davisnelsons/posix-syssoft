#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h> 
#include <string.h>

#include "config.h"
#include "sensor_db.h"


#ifdef DEBUG
#define DEBUG_PRINTF(...) 									                                        \
        do {											                                            \
            fprintf(stderr,"\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__);	    \
            fprintf(stderr,__VA_ARGS__);								                            \
            fflush(stderr);                                                                         \
                } while(0)
#else
#define DEBUG_PRINTF(...) (void)0
#endif


DBCONN *init_connection(char clear_up_flag, status_code_storagemgr_t *status, char *table_name) {
    
    sqlite3 *db;
    int rc = sqlite3_open(DB_NAME, &db);
    if (rc != SQLITE_OK) {
        DEBUG_PRINTF("cannot open db");
        sqlite3_close(db);
        *status = STATUS_FAILURE;
        return NULL;
    }
    if(clear_up_flag) {
        //clear table
        char *query = "DROP TABLE IF EXISTS "
                    TABLE_NAME
                    ";"
                    "CREATE TABLE "
                    TABLE_NAME
                    " (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "sensor_id INTEGER,"
                    "sensor_value DECIMAL(4,2), "
                    "timestamp TIMESTAMP);";
        char *err_msg;
        rc = sqlite3_exec(db, query, 0, 0, &err_msg);
        if(rc != SQLITE_OK) {
            DEBUG_PRINTF("cannot access data, %s", err_msg);
            sqlite3_free(err_msg);
            sqlite3_close(db);
            *status = STATUS_FAILURE;
            return NULL;
        }
        *status = STATUS_NEW_TABLE;
        strcpy(table_name, TABLE_NAME);
        return db;//TODO STATUS_OK
    } else {
        *status = STATUS_OK;
        return db;
    }
}

void disconnect(DBCONN *db) {
    sqlite3_close(db);
    
}
status_code_storagemgr_t insert_sensor(DBCONN *conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts) {
    
    char stringified[64];
    char *err_msg;
    int rc;
    sprintf(stringified, "(%d, %.2f, %lld);", (int) id, (double) value, (long long)ts);
    char query[2048];
    sprintf(query, "INSERT INTO " TABLE_NAME "(sensor_id, sensor_value, timestamp) VALUES %s ", stringified);
    DEBUG_PRINTF("executing query: %s", query);
    rc = sqlite3_exec(conn, query, 0, 0, &err_msg);
    if(rc != SQLITE_OK) {
        DEBUG_PRINTF("cannot insert data, %s", err_msg);
        sqlite3_free(err_msg);
        return STATUS_FAILURE;
    }
    
    return STATUS_OK;
    
}





























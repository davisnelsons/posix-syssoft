/**
 * \author Dāvis Edvards Nelsons
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#ifndef SET_MAX_TEMP
#error SET_MAX_TEMP not set
#endif

#ifndef SET_MIN_TEMP
#error SET_MIN_TEMP not set
#endif

#ifndef TIMEOUT
#define TIMEOUT 10
#endif

#ifndef MAX_CONN
#define MAX_CONN 64
#endif

#ifndef RUN_AVG_LENGTH
#define RUN_AVG_LENGTH 5
#endif


#ifndef DB_NAME
#define DB_NAME "Sensor.db"
#endif

#ifndef TABLE_NAME
#define TABLE_NAME "SensorData"
#endif

#define TIMEDWAIT_LENGTH 10

#ifndef CLEAR_DB
#define CLEAR_DB 1
#endif

#define MAX 120 //max length of log msg


#include <stdint.h>
#include <time.h>

typedef uint16_t sensor_id_t;
typedef double sensor_value_t;
typedef time_t sensor_ts_t;         // UTC timestamp as returned by time() - notice that the size of time_t is different on 32/64 bit machine

typedef struct {
    sensor_id_t id;
    sensor_value_t value;
    sensor_ts_t ts;
} sensor_data_t;



#endif /* _CONFIG_H_ */

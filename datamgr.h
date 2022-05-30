/**
 * \author Dāvis Edvards Nelsons
 */

#ifndef DATAMGR_H_
#define DATAMGR_H_

#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "lib/dplist.h"




/*
 * Use ERROR_HANDLER() for handling memory allocation problems, invalid sensor IDs, non-existing files, etc.
 */
#define ERROR_HANDLER(condition, ...)    do {                       \
                      if (condition) {                              \
                        printf("\nError: in %s - function %s at line %d: %s\n", __FILE__, __func__, __LINE__, __VA_ARGS__); \
                        exit(EXIT_FAILURE);                         \
                      }                                             \
                    } while(0)
                    
typedef uint16_t room_id_t;

/*
 * set up temp_buffer
 */

typedef struct temp_buffer temp_buffer_t;
typedef struct temp_buffer_node temp_buffer_node_t;

/*
 * set up status return type
 */
enum datamgr_status {DATAMGR_OK = 0, DATAMGR_FAILURE, DATAMGR_MEM_ERROR, DATAMGR_WRONG_ID };
typedef enum datamgr_status datamgr_status_t;

 
/**
 * structure to hold sensor data solely for use in datamgr
 */
typedef struct {
    sensor_id_t id;         /** < sensor id */
    temp_buffer_t *buffer;   /** < sensor value buffer - a cyclic buffer retaining only last RUN_AVG_LENGTH sensor values */
    sensor_ts_t ts;         /** < sensor timestamp */
} sensor_data_mgr_t;



/**
 *  This method holds the core functionality of your datamgr. It takes in 2 file pointers to the sensor files and parses them. 
 *  When the method finishes all data should be in the internal pointer list and all log messages should be printed to stderr.
 *  \param fp_sensor_map file pointer to the map file
 *  \param fp_sensor_data file pointer to the binary data file
 *  \return status
 */
datamgr_status_t datamgr_parse_sensor_files(FILE *fp_sensor_map, dplist_t **data_list);

/**
 * This method should be called to clean up the datamgr, and to free all used memory. 
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free();

/**
 * Gets the room ID for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the corresponding room id
 */
uint16_t datamgr_get_room_id(sensor_id_t sensor_id);

/**
 * Gets the running AVG of a certain sensor ID (if less then RUN_AVG_LENGTH measurements are recorded the avg is 0)
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the running AVG of the given sensor
 */
datamgr_status_t datamgr_get_avg(dplist_t *data_list, sensor_id_t sensor_id, sensor_value_t *av);

/**
 * Returns the time of the last reading for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 * \param sensor_id the sensor id to look for
 * \return the last modified timestamp for the given sensor
 */
time_t datamgr_get_last_modified(sensor_id_t sensor_id);

/**
 *  Return the total amount of unique sensor ID's recorded by the datamgr
 *  \return the total amount of sensors
 */
int datamgr_get_total_sensors();

/*
 * inserts sensor data in sensor_data from data_list, found by sensor_id
 *
 */
datamgr_status_t datamgr_get_sensor_data_from_sensorid(dplist_t *data_list, sensor_id_t sensor_id, sensor_data_mgr_t **sensor_data);

/*
 * inserts new received sensor reading from the sbuffer into data_list
 */
int datamgr_insert_new_sensor_reading(dplist_t *data_list, sensor_id_t sensor_id, sensor_value_t value, sensor_ts_t ts);



#endif  //DATAMGR_H_

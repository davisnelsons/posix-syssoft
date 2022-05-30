/**
 * \author Dāvis Edvards Nelsons
 */

#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#include <pthread.h>
#include "config.h"
#include "fifo_pipe.h"


#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1
#define SBUFFER_MORE_AVAILABLE 2
#define SBUFFER_TIMEOUT 3

#define DATAMGR_ID 0
#define STORAGEMGR_ID 1

typedef struct sbuffer sbuffer_t;
typedef struct sbuffer_node sbuffer_node_t;


//should not be here
struct arg_struct_connmgr {
    int port;
    sbuffer_t *sb;
    FILE *fp_sensor_map;
    pthread_mutex_t *lock;
    int *stop_threads_flag;
    fifo_pipe_t *log_pipe;
};
typedef struct arg_struct_connmgr arg_struct_connmgr_t;

/**
 * Allocates and initializes a new shared buffer
 * \param buffer a double pointer to the buffer that needs to be initialized
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_init(sbuffer_t **buffer);

/**
 * All allocated resources are freed and cleaned up
 * \param buffer a double pointer to the buffer that needs to be freed
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_free(sbuffer_t **buffer);

/**
 * Removes the first sensor data in 'buffer' (at the 'head') and returns this sensor data as '*data'
 * If 'buffer' is empty, the function doesn't block until new sensor data becomes available but returns SBUFFER_NO_DATA
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to pre-allocated sensor_data_t space, the data will be copied into this structure. No new memory is allocated for 'data' in this function.
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_remove(sbuffer_t *buffer, sbuffer_node_t *node);

/**
 * Inserts the sensor data in 'data' at the end of 'buffer' (at the 'tail')
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to sensor_data_t data, that will be copied into the buffer
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
*/
int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data);
/*
 * reads node from sbuffer that has not been read already by this reader
 *
 * Can do it in blocking and nonblocking mode
 *
 * blocking -> wait for signal from arg_struct_connmgr
 *
 * non-blocking-> dont wait for signal, grab lock and see if it is possible to read
 *
 * after read mark flag to signify this node is read by reader_id
 *
 */
int sbuffer_read(sbuffer_t *buffer, sensor_data_t *data, int reader_id, char blocking);

/*
 * remove nodes that are read by both readers already
 */
int sbuffer_remove_read(sbuffer_t *buffer);

/*
 * clear sbuffer
 */
int sbuffer_clear(sbuffer_t **buffer);

#endif  //_SBUFFER_H_

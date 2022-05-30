#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "config.h"
#include "datamgr.h"
#include "lib/dplist.h"

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

typedef struct {
    room_id_t room_id;
    sensor_data_mgr_t sensor_data;
} my_element_t;


struct temp_buffer {
    temp_buffer_node_t *head;
    int count;  //how many readings are in the buffer
};

struct temp_buffer_node {
    temp_buffer_node_t *next;
    sensor_value_t value;
};



void* element_copy(void * element);
void element_free(void ** element);
int element_compare(void * x, void * y);
void free_cyc_buffer(temp_buffer_t **buffer);

//not used
void * element_copy(void * element) {
    return NULL;
}

void element_free(void ** element) {
    //DEBUG_PRINTF("using element_free");
    my_element_t *el = (my_element_t *) *element;
    free_cyc_buffer(&(el->sensor_data.buffer));
    el->sensor_data.buffer = NULL;
    free( (my_element_t *) *element);
    return;
}

void free_cyc_buffer(temp_buffer_t **buffer) {
    if(*buffer == NULL) return;
    temp_buffer_t *buf = *buffer;
    if(buf->head == NULL) {
        free(buf);
        return;
    }
    temp_buffer_node_t *dummy = buf->head;
    while(dummy->next != NULL) {
        temp_buffer_node_t *to_remove = dummy;
        dummy = dummy->next;
        free(to_remove);
    }
    free(dummy);
    free(buf);
}

int element_compare(void * x, void * y) {
    //compares by sensor_id
    sensor_id_t x_id = ((my_element_t*)x)->sensor_data.id;
    sensor_id_t y_id = ((my_element_t*)y)->sensor_data.id;
    if(x_id == y_id) {
        //DEBUG_PRINTF("element compare found that %i is equal to %i",x_id, y_id);
        return 1;
    }
    //DEBUG_PRINTF("element compare found that %i is not equal to %i", x_id, y_id);
    return 0;
}



datamgr_status_t datamgr_parse_sensor_files(FILE *fp_sensor_map, dplist_t **data_list) {

    *data_list = dpl_create(element_copy, element_free, element_compare);
    if(data_list == NULL) return DATAMGR_FAILURE;
    char * line = NULL;
    size_t length = 0;
    size_t read;
    while ((read = getline(&line, &length, fp_sensor_map)) != -1) {
        int room_nr, sens_id;
        my_element_t *new_el = malloc(sizeof(my_element_t));//TODO implement element_copy
        if(new_el == NULL) return DATAMGR_MEM_ERROR;
        sscanf(line, "%d %d", &room_nr, &sens_id);
        new_el->room_id = (room_id_t) room_nr;
        new_el->sensor_data.id = sens_id;
        new_el->sensor_data.buffer=malloc(sizeof(temp_buffer_t));
        if(new_el->sensor_data.buffer == NULL) return DATAMGR_MEM_ERROR;
        new_el->sensor_data.buffer->head = NULL;
        new_el->sensor_data.buffer->count = 0;
        *data_list = dpl_insert_at_index(*data_list, new_el, 0, false);
        if(*data_list==NULL) return DATAMGR_FAILURE;
    }
    free(line);
    return DATAMGR_OK;
}


datamgr_status_t datamgr_get_sensor_data_from_sensorid(dplist_t *data_list, sensor_id_t sensor_id, sensor_data_mgr_t **s_data) {

    my_element_t *synth_element = malloc(sizeof(my_element_t));
    if(synth_element == NULL) return DATAMGR_MEM_ERROR;
    synth_element->sensor_data.id = sensor_id;
    int index = dpl_get_index_of_element(data_list,synth_element);
    if(index == -1) return DATAMGR_WRONG_ID;
    my_element_t *found_el = dpl_get_element_at_index(data_list, index);//
    if(found_el != NULL) {
        free(synth_element);
        *s_data = &(found_el->sensor_data);
        return DATAMGR_OK;
    }
    return DATAMGR_FAILURE;
}


datamgr_status_t datamgr_get_avg(dplist_t *data_list, sensor_id_t sensor_id, sensor_value_t *av) {

    sensor_data_mgr_t *sensor_data;
    datamgr_status_t status = datamgr_get_sensor_data_from_sensorid(data_list, sensor_id, &sensor_data);
    if(status == DATAMGR_FAILURE || status == DATAMGR_MEM_ERROR) return status;
    sensor_value_t average = 0;
    if(sensor_data->buffer->count == RUN_AVG_LENGTH) {
        //buffer full
        temp_buffer_node_t *dummy = sensor_data->buffer->head;
        while(dummy != NULL) {
            average = average + dummy->value;
            dummy=dummy->next;
        }
        average = average/RUN_AVG_LENGTH;
    } else {
        //buffer not full yet, have to return 0
        average = 0;
    }
    *av = average;
    return DATAMGR_OK;
}


int datamgr_insert_new_sensor_reading(dplist_t *data_list, sensor_id_t sensor_id, sensor_value_t value, sensor_ts_t ts) {

    if(data_list == NULL) return DATAMGR_FAILURE;
    //sanity check inserted values
    if(sensor_id < 0 || value > 70 || value < -70 || ts < 1640000000 || ts > 10000000000) {
        DEBUG_PRINTF("rejected sensor reading due to sanity check fail");
        return DATAMGR_FAILURE;
    }
    sensor_data_mgr_t *sensor_data;
    datamgr_status_t status = datamgr_get_sensor_data_from_sensorid(data_list, sensor_id, &sensor_data);
    if(status != DATAMGR_OK){
        return status;
    }

    sensor_data->ts = ts;

    int readings_done = sensor_data->buffer->count;

    if(readings_done < RUN_AVG_LENGTH) {
        //not yet full buffer
        //add new node to the end of buffer
        temp_buffer_node_t *new_node = malloc(sizeof(temp_buffer_node_t));
        if(new_node == NULL) return DATAMGR_MEM_ERROR;
        new_node->value = value;
        new_node->next = NULL;

        temp_buffer_node_t *dummy = sensor_data->buffer->head;
        if(dummy==NULL) {
            sensor_data->buffer->head = new_node;
            sensor_data->buffer->count++;
            return DATAMGR_OK;
        } else {
            while(dummy->next != NULL) {//crawl through buffer until the last node
                dummy= dummy->next;
            }
            dummy->next = new_node;
            sensor_data->buffer->count++;
            return DATAMGR_OK; //fix return values
        }
    } else {
        //buffer already full, get rid of oldest node and add the newest
        temp_buffer_node_t *new_node = malloc(sizeof(temp_buffer_node_t));
        if(new_node == NULL) return DATAMGR_MEM_ERROR;
        new_node->value = value;
        new_node->next = NULL;
        temp_buffer_node_t *old = sensor_data->buffer->head;
        sensor_data->buffer->head = old->next;
        free(old);
        temp_buffer_node_t *dummy = sensor_data->buffer->head;
        while(dummy->next != NULL) {//crawl through buffer until the last node
                dummy= dummy->next;
            }
        dummy->next = new_node;
        return DATAMGR_OK; //fix return values
    }
}





















































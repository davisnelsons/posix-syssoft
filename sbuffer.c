/**
 * \author Dāvis Edvards Nelsons
 */

#include <stdlib.h>
#include <stdio.h>
#include "sbuffer.h"
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include "config.h"



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
/**
 * basic node for the buffer, these nodes are linked together to create the buffer
 */
struct sbuffer_node {
    struct sbuffer_node *next;  /**< a pointer to the next node*/
    sensor_data_t data;         /**< a structure containing the data */
    unsigned char read_count[2];
};

/**
 * a structure to keep track of the buffer
 */
struct sbuffer {
    sbuffer_node_t *head;       /**< a pointer to the first node in the buffer */
    sbuffer_node_t *tail;       /**< a pointer to the last node in the buffer */
    pthread_mutex_t lock1;
    pthread_mutex_t lock2;
    pthread_cond_t new_unread1;
    pthread_cond_t new_unread2;
};

int sbuffer_init(sbuffer_t **buffer) {
    *buffer = malloc(sizeof(sbuffer_t));
    if (*buffer == NULL) return SBUFFER_FAILURE;
    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    pthread_mutex_init(&((*buffer)->lock1), NULL);
    pthread_mutex_init(&((*buffer)->lock2), NULL);
    pthread_cond_init(&((*buffer)->new_unread1), NULL);
    pthread_cond_init(&((*buffer)->new_unread2), NULL);
    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer) {
    if ((buffer == NULL) || (*buffer == NULL)) {
        return SBUFFER_FAILURE;
    }
    while ((*buffer)->head) {
        sbuffer_node_t *dummy = (*buffer)->head;
        (*buffer)->head = (*buffer)->head->next;
        free(dummy);
    }
    free(*buffer);
    *buffer = NULL;
    return SBUFFER_SUCCESS;
}

int sbuffer_remove(sbuffer_t *buffer, sbuffer_node_t *node) {

    if (buffer->head == NULL) return SBUFFER_NO_DATA;
    if (buffer->head == buffer->tail) // buffer has only one node
   {
        DEBUG_PRINTF("buffer only has one node");
        free(buffer->head);
        buffer->head = buffer->tail = NULL;
        return SBUFFER_SUCCESS;
   }

   sbuffer_node_t *dummy = buffer->head;
   while(dummy != node) {
       DEBUG_PRINTF("buffer has multiple nodes");
       buffer->head = dummy->next;
       free(dummy);
       dummy = buffer->head;
   }

   if(dummy->next != NULL) {
        buffer->head = dummy->next;
        free(dummy);
    } else {
        buffer->head=buffer->tail=NULL;
        free(dummy);
    }
    return SBUFFER_SUCCESS;
}



int sbuffer_read(sbuffer_t *buffer, sensor_data_t *data, int reader_id, char blocking) {

    sbuffer_node_t *dummy;
    pthread_mutex_t *my_lock;
    pthread_cond_t *my_cond;
    //nonextensible to more than 2 readers
    if(reader_id == STORAGEMGR_ID) {
        //successfuly locked lock1
        //storagemgr
        pthread_mutex_lock(&(buffer->lock1));
        my_lock = &(buffer->lock1);
        my_cond = &(buffer->new_unread1);
        //locked = 1;
        DEBUG_PRINTF("lock1 taken by %i", reader_id);

    } else if (reader_id == DATAMGR_ID) {
        //datamgr
        pthread_mutex_lock(&(buffer->lock2));
        my_lock = &(buffer->lock2);
        my_cond = &(buffer->new_unread2);
        //locked = 1;
        DEBUG_PRINTF("lock2 taken by %i", reader_id);
    }
    if(blocking) {
        //timing code inspired by ibm.com/docs/en/i/7.4?topic=ssw_ibm_i_74/apis/users_77.htm
        struct timespec ts;
        struct timeval tp;

        gettimeofday(&tp, NULL);

        ts.tv_sec = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec*100;
        ts.tv_sec += TIMEDWAIT_LENGTH; //waits for TIMEDWAIT_LENGTH secs before return
        int ret = pthread_cond_timedwait(my_cond, my_lock, &ts);
        if(ret == ETIMEDOUT) {
            pthread_mutex_unlock(my_lock);
            DEBUG_PRINTF("timeout on cond var");
            return SBUFFER_TIMEOUT;
        } else {
            DEBUG_PRINTF("recieved signal and lock taken by %i", reader_id);
        }
    }
    dummy = buffer->head;
    int status = 1;
    while(status){
        if(dummy == NULL) {
            pthread_mutex_unlock(my_lock);
            DEBUG_PRINTF("unlock by %i", reader_id);
            return SBUFFER_FAILURE;
        }
        //not read yet
        if(dummy->read_count[reader_id] == 0) {
            dummy->read_count[reader_id] = 1;
            *data = dummy->data;
            status = 0;
            if(dummy->next != NULL) {
                DEBUG_PRINTF("more avail");
                pthread_mutex_unlock(my_lock);
                DEBUG_PRINTF("unlock by %i", reader_id);
                return SBUFFER_MORE_AVAILABLE;
            } else {
                DEBUG_PRINTF("no more avail");
                pthread_mutex_unlock(my_lock);
                DEBUG_PRINTF("unlock by %i", reader_id);
                return SBUFFER_SUCCESS;
            }
        }
        //checked all nodes and found no unread nodes
        if(dummy == buffer->tail) {
            pthread_mutex_unlock(my_lock);
            DEBUG_PRINTF("unlock by %i", reader_id);
            return SBUFFER_NO_DATA;
        }
        if(dummy->next == NULL){
            pthread_mutex_unlock(my_lock);
            DEBUG_PRINTF("unlock by %i", reader_id);
            return SBUFFER_NO_DATA;
        }
        dummy = dummy->next;
    }
    return SBUFFER_FAILURE;
}


int sbuffer_insert(sbuffer_t *buffer, sensor_data_t *data) {
    DEBUG_PRINTF("inserting ...");
    pthread_mutex_lock(&(buffer->lock1));
    DEBUG_PRINTF("lock1 taken by write");
    pthread_mutex_lock(&(buffer->lock2));
    DEBUG_PRINTF("lock2 taken by write");
    sbuffer_node_t *dummy;
    if (buffer == NULL) return SBUFFER_FAILURE;
    dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL) return SBUFFER_FAILURE;
    dummy->data = *data;
    dummy->next = NULL;
    memset(dummy->read_count, 0, sizeof(unsigned char)*2);
    if (buffer->tail == NULL) // buffer empty (buffer->head should also be NULL
    {
        //DEBUG_PRINTF("buffer empty");
        buffer->head = buffer->tail = dummy;

    } else // buffer not empty
    {
        buffer->tail->next = dummy;
        buffer->tail = buffer->tail->next;
        //scan sbuffer for fully read nodes that need to be removed
        dummy = buffer->head;
        while(dummy->next != NULL) {
            if(dummy->read_count[0] == 1 && dummy->read_count[1] == 1) {
                sbuffer_node_t *dummy_to_remove = dummy;
                DEBUG_PRINTF("node found to remove, id: %i, value: %.2f, ts: %ld", dummy->data.id, dummy->data.value, dummy->data.ts);
                dummy= dummy->next;

                sbuffer_remove(buffer, dummy_to_remove);
            } else {
                dummy= dummy->next;
            }
        }
    }
    pthread_cond_broadcast(&(buffer->new_unread1)); //might be possible with single cond var
    pthread_cond_broadcast(&(buffer->new_unread2));
    pthread_mutex_unlock(&(buffer->lock1));
    DEBUG_PRINTF("lock1 unlocked by write");
    pthread_mutex_unlock(&(buffer->lock2));
    DEBUG_PRINTF("lock2 unlocked by write");
    return SBUFFER_SUCCESS;
}

int sbuffer_remove_read(sbuffer_t *buffer) {
    sbuffer_node_t *dummy, *dummy_to_remove;
    if(buffer->head == NULL) return SBUFFER_SUCCESS;
    dummy = buffer->head;
    while(dummy->next != NULL) {
            if(dummy->read_count[0] == 1 && dummy->read_count[1] == 1) {
                dummy_to_remove = dummy;
                DEBUG_PRINTF("node found to remove, id: %i, value: %.2f, ts: %ld", dummy->data.id, dummy->data.value, dummy->data.ts);
                dummy= dummy->next;
                sbuffer_remove(buffer, dummy_to_remove);
            } else {
                dummy= dummy->next;
            }
    }
    return SBUFFER_SUCCESS;
}

int sbuffer_clear(sbuffer_t **buffer) {
    sbuffer_t *buf_p = *buffer;
    if(buf_p->head == NULL) {
        free(buf_p);
        buf_p = NULL;
        buffer=NULL;
        return SBUFFER_SUCCESS;
    }
    sbuffer_node_t *dummy = buf_p->head;
    sbuffer_node_t *dummy_to_remove;
    while(dummy->next != NULL) {
        dummy_to_remove = dummy;
        dummy = dummy->next;
        sbuffer_remove(*buffer, dummy_to_remove);
        DEBUG_PRINTF("removing node");
    }
    free(dummy);
    dummy=NULL;
    dummy_to_remove=NULL;
    free(buf_p);
    buf_p = NULL;
    return SBUFFER_SUCCESS;


}













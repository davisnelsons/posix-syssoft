/**
 * \author Dāvis Edvards Nelsons
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "lib/tcpsock.h"
#include "connmgr.h"
#include "lib/dplist.h"
#include <pthread.h>
#include "sbuffer.h"
#include "datamgr.h"
#include <unistd.h>
#include "sensor_db.h"
#include <string.h>
#include "fifo_pipe.h"
#include <limits.h>
#include <assert.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/wait.h>
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

//

fifo_pipe_t *log_pipe;
sbuffer_t *shared_buffer;
dplist_t *datamgr_list; //TODO maybe put in datamgr_thread func

pthread_mutex_t *stop_threads;
int *stop_threads_flag;

pthread_mutex_t *stop_log;
int *stop_log_flag;
struct arg_struct_datamgr {
    FILE *fp_sensor_map;
};
typedef struct arg_struct_datamgr arg_struct_datamgr_t;

void kill_gateway() {
    pthread_mutex_lock(stop_threads);
    *stop_threads_flag = 1;
    pthread_mutex_unlock(stop_threads);
}

void mem_fail() {

    printf("FATAL ERROR: memory failure, stopping gateway");
    fifo_pipe_write(log_pipe, "memory failure, stopping gateway");
    kill_gateway();
}

void datamgr_log(char *msg) {

    char *mod_msg;
    asprintf(&mod_msg, "DATAMGR_THREAD: %s", msg);
    fifo_pipe_write(log_pipe, mod_msg);
    free(mod_msg);
}

void datamgr_log_temp(char *msg, sensor_id_t id, sensor_value_t average) {

    char *mod_msg;
    asprintf(&mod_msg, "DATAMGR_THREAD: %s (sensor id = %i, average temp = %.2f)", msg, id, average);
    fifo_pipe_write(log_pipe, mod_msg);
    free(mod_msg);
}

void *datamgr_thread(void *args) {

    arg_struct_datamgr_t *arg_datamgr = (arg_struct_datamgr_t *) args;
    datamgr_status_t dmgr_status;
    dmgr_status = datamgr_parse_sensor_files(arg_datamgr->fp_sensor_map, &datamgr_list);//arg_connmgr->fp_sensor_map

    if(dmgr_status == DATAMGR_FAILURE) {
        datamgr_log("failed to parse sensor map file");
        kill_gateway();
    }
    if(dmgr_status == DATAMGR_MEM_ERROR) {
        datamgr_log("failure to allocate memory");
        kill_gateway();
    }

    int reader_id = DATAMGR_ID;
    sensor_data_t *read_data = malloc(sizeof(sensor_data_t));
    if(read_data == NULL) mem_fail();
    //starting values
    read_data->id = 0;
    read_data->value = 0.00;
    read_data->ts = 0;
    sensor_value_t average = 0;

    int st_flag, sbuffer_status;
    //Big loop, exits only on threadstop
    while(1) {
        //check if thread should be killed instead
        pthread_mutex_lock(stop_threads);
        st_flag = *stop_threads_flag;
        pthread_mutex_unlock(stop_threads);
        if(st_flag) {
            //clean up storagemgr, exit
            dpl_free(&datamgr_list, true);
            datamgr_log("exiting datamgr..");
            free(read_data);
            pthread_exit(NULL);
        }
        sbuffer_status = sbuffer_read(shared_buffer, read_data, reader_id, 1);
        //reads while the sbuffer signals still available nodes
        while(sbuffer_status == SBUFFER_MORE_AVAILABLE || sbuffer_status == SBUFFER_SUCCESS) {

            DEBUG_PRINTF("datamgr thread received sensdata with id: %i, value: %.2f, ts: %ld", read_data->id, read_data->value, read_data->ts);
            dmgr_status = datamgr_insert_new_sensor_reading(datamgr_list, read_data->id, read_data->value, read_data->ts);
            //check if sensor inserted correctly
            if(dmgr_status == DATAMGR_MEM_ERROR) mem_fail();
            if(dmgr_status == DATAMGR_WRONG_ID) {
                char *msg;
                asprintf(&msg, "received data with missing sensor_id: %i, please check room sensor map file. Data stored in the database", read_data->id);
                datamgr_log(msg);
                free(msg);
                break;
            }
            if(dmgr_status == DATAMGR_FAILURE){
                DEBUG_PRINTF("failure to insert datamgr reading");
                datamgr_log("failure to insert datamgr reading");
                break;
            }

            dmgr_status = datamgr_get_avg(datamgr_list, read_data->id, &average);
            if(dmgr_status == DATAMGR_MEM_ERROR) mem_fail();
            if(dmgr_status == DATAMGR_FAILURE){
                DEBUG_PRINTF("failure to calculate average");
                datamgr_log("failure to calculate average");
                break;
            }
            DEBUG_PRINTF("average for sensorid: %i is %.2f", read_data->id, average);
            if(average > SET_MAX_TEMP) {
                datamgr_log_temp("too hot temp", read_data->id, average);
                printf("Temperature is too high: sensor_id = %i, average temperature = %.2f\n", read_data->id, average);
            } else if (average < SET_MIN_TEMP) {
                datamgr_log_temp("too cold temp", read_data->id, average);
                printf("Temperature is too low: sensor_id = %i, average temperature = %.2f\n", read_data->id, average);
            }
            sbuffer_status = sbuffer_read(shared_buffer, read_data, reader_id, 1);
        }

        if(sbuffer_status == SBUFFER_TIMEOUT) {
            DEBUG_PRINTF("sbuffer timeout in datamgr");
        } else if(sbuffer_status == SBUFFER_NO_DATA) {
            DEBUG_PRINTF("sbuffer no data");
        }
    }
    return NULL;
}

void storagemgr_log(char *msg) {

    char *mod_msg;
    asprintf(&mod_msg, "STORAGE_THREAD: %s", msg);
    fifo_pipe_write(log_pipe, mod_msg);
    free(mod_msg);
}
void *storagemgr_thread(void *args) {

    DBCONN *db;
    status_code_storagemgr_t *status = malloc(sizeof(status_code_storagemgr_t));
    if(status==NULL) mem_fail();
    char *table_name = malloc(16*sizeof(char));
    if(table_name == NULL) mem_fail();
    int fail_count = 0;
    do{
        db = init_connection(CLEAR_DB, status, table_name); //by default db is cleared every time
        if(*status == STATUS_NEW_TABLE) {
            storagemgr_log("Connection to DB estabilished successfuly");
            char *buf;
            asprintf(&buf, "new table %s created", table_name);
            storagemgr_log(buf);//TODO full logging capabilities
            free(buf);
        } else if(*status == STATUS_OK) {
            storagemgr_log("Connection to DB estabilished successfuly");
        } else {
            storagemgr_log("Connection to DB failed");
            fail_count++;
            sleep(2);
        }
    }
    while(*status == STATUS_FAILURE && fail_count<3);
    free(table_name);

    if(fail_count == 3 && *status == STATUS_FAILURE) {
        storagemgr_log("Connection to DB failed 3 times, stopping gateway..");
        kill_gateway();
    }

    int reader_id = STORAGEMGR_ID;

    sensor_data_t *read_data = malloc(sizeof(sensor_data_t));
    if(read_data == NULL) mem_fail();
    read_data->id = 0;
    read_data->value = 0.0;
    read_data->ts = 0;

    char blocking = 1; //read mode
    char cleanup = 1; //one-shot final cleanup
    int MAX_READS = 8;  //control how many reads to do in one "go"
    int sleep_length = 7;  //sleep after one "go"

    int sbuffer_status = 0;
    int counter = 0;    //counts reads currently done
    int st_flag = 0;
    while(1) {
        //receive data from the sbuffer
        do {
            sbuffer_status = sbuffer_read(shared_buffer, read_data, reader_id, blocking);
            if(sbuffer_status == SBUFFER_NO_DATA || sbuffer_status == SBUFFER_FAILURE || sbuffer_status == SBUFFER_TIMEOUT) break;
            DEBUG_PRINTF("storagemgr thread received sensdata with id: %i, value: %.2f, ts: %ld", read_data->id, read_data->value, read_data->ts);
            *status = insert_sensor(db, read_data->id, read_data->value, read_data->ts);
            if(*status == STATUS_OK) {
                storagemgr_log("new reading inserted successfully");
               // storagemgr_log("race condition check");
            } else {
                storagemgr_log("storagemgr lost connection to DB");
            }
            if(blocking) break; //break out of blocking mode on first successful read;
            counter++;
        } while((sbuffer_status == SBUFFER_MORE_AVAILABLE || sbuffer_status == SBUFFER_SUCCESS) && counter < MAX_READS);

        //autoscale read size/speed up and down
        if(sbuffer_status == SBUFFER_MORE_AVAILABLE) {
            DEBUG_PRINTF("speeding up storagemgr..");
            MAX_READS++;
            sleep_length--;
            if(sleep_length < 1) sleep_length = 1; // keep sane limits
            if(MAX_READS > 50) MAX_READS = 50;
        }
        if(sbuffer_status == SBUFFER_SUCCESS && (counter+3 < MAX_READS)) { //the +3 is arbritary
            DEBUG_PRINTF("slowing down storagemgr...");
            sleep_length++;
            MAX_READS--;
            if(sleep_length > 20) sleep_length = 20; //keep sane limits
            if(MAX_READS < 5) MAX_READS = 5;
        }

        /*
         * Control blocking mode and non-blocking mode
         * Blocking - when no node is connected -> storagemgr will wait for signal from writer thread(connmgr)
         * Non-blocking - nodes connected, data is flowing -> storagemgr does not wait for signal, locks mutex, reads up to MAX_READS nodes, unlocks and sleeps for sleep_length until next lock
         */
        if(counter == 0 && (sbuffer_status == SBUFFER_NO_DATA||sbuffer_status == SBUFFER_FAILURE || sbuffer_status == SBUFFER_TIMEOUT) ) {
            //unsuccessful read, wait for write to continue
            blocking =1;
            DEBUG_PRINTF("blocking mode for storagemgr");
        } else {
            //all fine, keep working in non block mode
            blocking = 0;
            DEBUG_PRINTF("nonblock mode for storagemgr");
        }
        //check if thread should be killed instead
        pthread_mutex_lock(stop_threads);
        st_flag = *stop_threads_flag;
        pthread_mutex_unlock(stop_threads);
        if(st_flag) {
            //make sure all the nodes in sbuffer have been read
            if(cleanup) {
                DEBUG_PRINTF("exiting storagemgr");
                storagemgr_log("exiting storagemgr");
                disconnect(db);
                MAX_READS = INT_MAX; // will miss nodes in extreme use cases
                sleep_length = 0;
                cleanup = 0;
                continue;
            }

            disconnect(db);
            free(read_data);
            free(status);
            pthread_exit(NULL);
        }
        counter = 0;
        if (!blocking) sleep(sleep_length); //sleeps only in non-blocking mode
    }
}

//a bit wierd implementation but works
//better to move magic string termination thing to fifo_pipe.c
void log_process(char *magic_string) {
    fifo_pipe_close_input(log_pipe);
    FILE *fp = fopen("./gateway.log", "w");
    int result;
    char buff[MAX];
    //i set up a quick poll structure for this, a bit overkill
    struct pollfd fds[1];
    memset(fds, 0, sizeof(fds));
    fds[0].fd = fifo_pipe_getreadfd(log_pipe);
    fds[0].events = POLLIN;
    int enable_loop = 1;
    int status;
    while(enable_loop) {
    status = poll(fds, 1, 5000);
    if(status == 0) {

    } else {
        if(fds[0].revents != POLLIN) {
            DEBUG_PRINTF("logproc received %i instead of POLLIN, exiting..", fds[0].revents);
            exit(0);

        } else {
            result = fifo_pipe_read(log_pipe, buff, MAX);
            if(result>0) {
                if(strcmp(buff, magic_string) == 0) {
                    DEBUG_PRINTF("log process exiting\n");
                    close(fds[0].fd);
                    enable_loop = 0;
                    fflush(fp);
                    fclose(fp);
                    exit(0);
                }
                DEBUG_PRINTF("new log msg from main: \"%s\"", buff);
                fprintf(fp, "%s\n", buff );
                fflush(fp);

            }
        }
    }
    }
}



int main(int argc, char *argv[]) {

    int port;
    if(argc != 2) {
        port = 5678; // default port
        printf("using default port %i\n", port);
    } else {
        port = atoi(argv[1]);
    }


    pid_t log_pid;
    //create pipe
    if(fifo_pipe_init(&log_pipe) == -1) mem_fail();
    //random string generate
    int magic = 12345;
    char *magic_string;
    asprintf(&magic_string,"%p", &magic);
    log_pid = fork();
    if(log_pid == 0) {
        log_process(magic_string);
    }
    fifo_pipe_close_output(log_pipe);
    fifo_pipe_write(log_pipe, "MAIN: pipe is initialized");
    pthread_t pthread_id_connmgr, pthread_id_datamgr, pthread_id_storagemgr;
    stop_threads = malloc(sizeof(pthread_mutex_t));
    if(stop_threads == NULL) mem_fail();
    pthread_mutex_init(stop_threads, NULL);
    stop_threads_flag = malloc(sizeof(int));
    if(stop_threads_flag == NULL) mem_fail();
    *stop_threads_flag = 0;



    if(sbuffer_init(&shared_buffer) != SBUFFER_SUCCESS) {
        DEBUG_PRINTF("buffer not possible to open");
        fifo_pipe_write(log_pipe, "buffer not possible to open");
        exit(EXIT_FAILURE);
    }

    FILE *fp_sensor_map = fopen("./room_sensor.map", "r");
    if (fp_sensor_map == NULL) {
        DEBUG_PRINTF("sensor map not possible to open");
        fifo_pipe_write(log_pipe, "sensor map not possible to open");
        exit(EXIT_FAILURE);
    }

    arg_struct_connmgr_t *arg_connmgr = malloc(sizeof(arg_struct_connmgr_t));
    if(arg_connmgr == NULL) mem_fail();

    arg_connmgr->port = port;
    arg_connmgr->sb = shared_buffer;
    arg_connmgr->lock = stop_threads;
    arg_connmgr->stop_threads_flag = stop_threads_flag;
    arg_connmgr->log_pipe = log_pipe;

    arg_struct_datamgr_t *arg_datamgr = malloc(sizeof(arg_struct_datamgr_t));
    if(arg_datamgr == NULL) mem_fail();

    arg_datamgr->fp_sensor_map = fp_sensor_map;

    pthread_create(&pthread_id_connmgr, NULL, connmgr_listen,(void *) arg_connmgr);
    pthread_create(&pthread_id_datamgr, NULL, datamgr_thread, (void *) arg_datamgr);
    pthread_create(&pthread_id_storagemgr, NULL, storagemgr_thread, NULL);

    pthread_join(pthread_id_connmgr, NULL);
    pthread_join(pthread_id_datamgr, NULL);
    pthread_join(pthread_id_storagemgr, NULL);

    sbuffer_clear(&shared_buffer);

    DEBUG_PRINTF("all threads exited");
    //now kill the log process (unreliable on mem_fail())
    fifo_pipe_write_magic(log_pipe, magic_string); //writing magic_string to pipe terminates log_process
    wait(NULL);
    free(stop_threads);
    free(stop_threads_flag);
    free(magic_string);
    free(arg_connmgr);
    free(arg_datamgr);
}

/**
 * \author Dāvis Edvards Nelsons
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "config.h"
#include "lib/tcpsock.h"
#include "connmgr.h"
#include "lib/dplist.h"
#include <string.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include "sbuffer.h"
#include <pthread.h>
#include "fifo_pipe.h"

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

pthread_mutex_t *lock;
int *stop_thr_flag;
fifo_pipe_t *log_pipe_p;
dplist_t *socket_list;

void socket_free(void **element) {
    DEBUG_PRINTF("socket free");
    int ret = tcp_close((tcpsock_t **) element);
    if(ret != TCP_NO_ERROR) DEBUG_PRINTF("failed to clear socket, error: %i", ret);
    free((tcpsock_t *) *element);
}

void close_all_connections(dplist_t *socket_list) {
    dpl_free(&socket_list, true);
}

void log_connmgr(char *msg) {
    char *mod_msg;
    asprintf(&mod_msg, "CONNMGR_THREAD: %s", msg);
    fifo_pipe_write(log_pipe_p, mod_msg);
    free(mod_msg);
}

void connmgr_mem_fail() {
    printf("memory failure in connmgr, stopping gateway... \n");
    pthread_mutex_lock(lock);
    *stop_thr_flag = 1;
    pthread_mutex_unlock(lock);
    close_all_connections(socket_list);
    pthread_exit(NULL);
}

void connmgr_fail() {
    printf("general failure in connmgr, stopping gateway... \n");
    pthread_mutex_lock(lock);
    *stop_thr_flag = 1;
    pthread_mutex_unlock(lock);
    close_all_connections(socket_list);
    pthread_exit(NULL);
}

void *connmgr_listen(void *args) {

    arg_struct_connmgr_t *arguments = (arg_struct_connmgr_t *) args;

    tcpsock_t *server, *client;
    sensor_data_t data;
    int bytes, result;
    int port =  arguments->port;
    socket_list = dpl_create(NULL, socket_free, NULL);
    if(socket_list == NULL) connmgr_mem_fail();
    sbuffer_t *shared_buffer = arguments->sb;
    lock = arguments->lock;
    stop_thr_flag = arguments->stop_threads_flag;
    log_pipe_p = arguments->log_pipe;

    //set up poll structure
    //this code is inspired by ibm.com/docs/en/i/7.4?topic=designs-using-poll-instead-select
    //and BeeJ's socket guide

    struct pollfd fds[MAX_CONN + 1]; // default MAX_CONN = 64; +1 is needed for the server socket

    //open the main server socket
    if (tcp_passive_open(&server, port) != TCP_NO_ERROR) connmgr_fail();
    memset(fds, 0, sizeof(fds));//clears the fds structure, protect against spurious revents on new socket creation
    int *server_sd = malloc(sizeof(int)); //
    if(server_sd == NULL) connmgr_mem_fail();
    if(tcp_get_sd(server, server_sd) != TCP_NO_ERROR) connmgr_fail(); // puts the socket descriptor of server into server_sd

    fcntl(*server_sd, F_SETFL, O_NONBLOCK); //set socket to nonblocking
    fds[0].fd = *server_sd;
    fds[0].events = POLLIN; //react on incoming connection
    free(server_sd);

    int nfds = 1;
    socket_list = dpl_insert_at_index(socket_list, (void*) server, 0, false); //insert server node at 0
    int thread_status;
    char compress_arrays = 0;
    int timeouts = 0;
    /* empirical func to find optimal polling timeout depending on the specified TIMEOUT
    * if TIMEOUT = 8, a single sensor node with 10 sec delay is caught in 2-4 passes
    * rendered useless if enough nodes are connected
        */
    int poll_time = (int) ((TIMEOUT * 1000)/3);
    printf("ready to receive sensor data\n");
    do {
        //timeout on no connection for some time
        if(timeouts < 4) { //4 is arbritary, might need to be increased?
            pthread_mutex_lock(lock);
            thread_status = *stop_thr_flag;
            pthread_mutex_unlock(lock);
            if(thread_status) {
                close_all_connections(socket_list);
                pthread_exit(NULL);
            }
        } else {
            log_connmgr("exiting connmgr due to no connections...");
            printf("gateway stopping due to no active connections..\n");
            pthread_mutex_lock(lock);
            *stop_thr_flag = 1;
            pthread_mutex_unlock(lock);
            close_all_connections(socket_list);
            pthread_exit(NULL);
        }
        //remove unused/disconnected nodes and compress fds and socket_list if node in the middle of list/array
        if(compress_arrays) {
            for(int i = 1; i <= nfds; i++) {
                if(fds[i].fd == -1) {
                    DEBUG_PRINTF("list size before: %i", dpl_size(socket_list));
                    socket_list = dpl_remove_at_index(socket_list, i, true);
                    DEBUG_PRINTF("list size after: %i", dpl_size(socket_list));
                    for(int j = 0; j < (nfds-i-1); j++) {
                        DEBUG_PRINTF("moving %i to %i",  i+j+1, i+j);
                        fds[i+j].fd = fds[i+j+1].fd;
                    }
                    nfds--;
                }
            }
            compress_arrays = 0;
        }

        DEBUG_PRINTF("polling..");
        int status = poll(fds, nfds, poll_time);
        if (status == 0){
            DEBUG_PRINTF("timeout");
            if(nfds == 1) timeouts++; //count timeouts only when no nodes connected
            sbuffer_remove_read(shared_buffer);
        }
        int current_nfds = nfds;
        DEBUG_PRINTF("open conns: %i", current_nfds);
        for(int i = 0; i < current_nfds; i++) {

            //check for socket inactivity
            if(fds[i].revents == 0 && i!=0) {
                //inactive socket for at least TIMEOUT
                client = (tcpsock_t *) dpl_get_element_at_index(socket_list, i);
                if( (TIMEOUT + (tcp_get_last_active(client))) < time(NULL)) {
                    DEBUG_PRINTF("removing node %i due to inactivity\n", i);
                    printf("removing node with sensor id = %i due to inactivity\n",tcp_get_sensor_id(client));
                    fds[i].fd = -1;
                    compress_arrays = 1;
                }
                continue;
            } else if( i == 0 && fds[i].revents == 0) {
                //sleeping socket at this time, TIMEOUT not reached
                continue;
            }
            //check for correct revent
            if(fds[i].revents != POLLIN) {
                DEBUG_PRINTF("failure: not a POLLIN, it is a %d for i = %i", fds[i].revents, i);
                client = (tcpsock_t *) dpl_get_element_at_index(socket_list, i);
                tcp_dump_sockinfo(client);//dump info on the socket
                continue;
            }
            //checks whether there is a connection to the main server node or to one of the sensor nodes
            if(i==0) { // if true then this is the main server socket that should accept the connection and create a new socket (no revent check because already done by ifs before)
                DEBUG_PRINTF("new incoming connection!");
                if(tcp_wait_for_connection(server, &client) != TCP_NO_ERROR){
                    DEBUG_PRINTF("error on waiting for connection");
                    continue;
                };
                DEBUG_PRINTF("tcp_wait_for_connection returned");
                int *client_sd = malloc(sizeof(int));
                if(client_sd == NULL) connmgr_mem_fail();
                if(tcp_get_sd(client, client_sd) != TCP_NO_ERROR) {
                    DEBUG_PRINTF("failed to get client_sd");
                    continue;
                };
                fds[nfds].fd = *client_sd;
                fds[nfds].events = POLLIN;
                nfds++;
                DEBUG_PRINTF("client with socket descriptor %i created", *client_sd);
                socket_list = dpl_insert_at_index(socket_list, (void*) client, current_nfds, false);
                timeouts = 0;
                free(client_sd);
            } else {
                timeouts = 0;
                DEBUG_PRINTF("received connection from a client, i = %i",i );
                client = (tcpsock_t *) dpl_get_element_at_index(socket_list, i);
                // read sensor ID
                bytes = sizeof(data.id);
                result = tcp_receive(client, (void *) &data.id, &bytes);
                if(result == TCP_CONNECTION_CLOSED) {
                    char *msg;
                    asprintf(&msg, "sensor node disconnected with id %i", tcp_get_sensor_id(client));
                    log_connmgr(msg);
                    printf("%s\n", msg);
                    free(msg);
                    fds[i].fd = -1;
                    if(i == (nfds -1)) {
                        //last node in list
                        DEBUG_PRINTF("removing last node in list");
                        socket_list = dpl_remove_at_index(socket_list, i, true);
                        nfds--;
                        continue;
                    } else {
                        //node aat start of list/in the middle of list
                        compress_arrays = 1;
                        continue;
                    }
                } else if(result != TCP_NO_ERROR) {
                    DEBUG_PRINTF("error while receiving from tcp");

                    continue;
                }
                if(!(tcp_is_id_received(client))) {
                    char *msg;
                    asprintf(&msg, "sensor node connected with id %i", data.id);
                    log_connmgr(msg);
                    free(msg);
                    tcp_set_sensor_id(client, data.id);
                }
                // read temperature
                bytes = sizeof(data.value);
                result = tcp_receive(client, (void *) &data.value, &bytes);
                if(result != TCP_NO_ERROR) continue;
                // read timestamp
                bytes = sizeof(data.ts);
                result = tcp_receive(client, (void *) &data.ts, &bytes);
                if(result != TCP_NO_ERROR) continue;
                if ((result == TCP_NO_ERROR) && bytes) {
//                     printf("sensor id = %" PRIu16 " - temperature = %g - timestamp = %ld\n", data.id, data.value,
//                         (long int) data.ts);
                    sbuffer_insert(shared_buffer, &data);
                }
            }
        }
    } while(1);
    return NULL;
}

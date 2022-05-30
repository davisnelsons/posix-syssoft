
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include "fifo_pipe.h"
#include <stdlib.h>
#include <string.h>



struct fifo_pipe{
    int pfds[2];
    pthread_mutex_t lock; //mutex to guard access to pipe
    long long sequence_nr;
};

int fifo_pipe_init(fifo_pipe_t **pipe_p) {
    *pipe_p = malloc(sizeof(fifo_pipe_t));
    if(*pipe_p == NULL) return -1;
    (*pipe_p)->sequence_nr = 1;
    pthread_mutex_init(&((*pipe_p)->lock), NULL);
    pipe((*pipe_p)->pfds); //TODO error checking
    return 1;
}

void fifo_pipe_write(fifo_pipe_t *pipe_p, char *send_buf) {
    time_t ts = time(NULL);
    char *msg;
    asprintf(&msg, "%lld %ld %s", pipe_p->sequence_nr, ts, send_buf);
    pthread_mutex_lock(&pipe_p->lock);
    pipe_p->sequence_nr++;
    write(pipe_p->pfds[1], msg, strlen(msg) + 1);
    pthread_mutex_unlock(&pipe_p->lock);
    free(msg);
}
void fifo_pipe_write_magic(fifo_pipe_t *pipe_p, char *send_buf) {
    char *msg;
    asprintf(&msg, "%s", send_buf);
    pthread_mutex_lock(&pipe_p->lock);
    pipe_p->sequence_nr++;
    write(pipe_p->pfds[1], msg, strlen(msg) + 1);
    pthread_mutex_unlock(&pipe_p->lock);
    free(msg);
}

void fifo_pipe_close_output(fifo_pipe_t *pipe_p) {
    close(pipe_p->pfds[0]);
}

void fifo_pipe_close_input(fifo_pipe_t *pipe_p) {
    close(pipe_p->pfds[1]);
}
int fifo_pipe_read(fifo_pipe_t *pipe_p, char *send_buf, int size) {
    //pthread_mutex_lock(&pipe_p->lock);
    int result = read((pipe_p->pfds)[0], send_buf, size);
   // pthread_mutex_unlock(&pipe_p->lock);
    return result;
}
int fifo_pipe_getreadfd(fifo_pipe_t *pipe_p) {
    return (pipe_p->pfds)[0];
}

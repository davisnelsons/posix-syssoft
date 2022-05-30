typedef struct fifo_pipe fifo_pipe_t;
/*
 * initialize fifo pipe
 */
int fifo_pipe_init(fifo_pipe_t **pipe_p);
//called by input side
void fifo_pipe_close_output(fifo_pipe_t *pipe_p);
//called by reader side
void fifo_pipe_close_input(fifo_pipe_t *pipe_p);
// write to pipe, thread safe by mutex
// adds sequence nr, timestamp to the log message
void fifo_pipe_write(fifo_pipe_t *pipe_p, char *send_buf);
// read from pipe, returns status
int fifo_pipe_read(fifo_pipe_t *pipe_p, char *send_buf, int size);
//get filedescriptor for the reader side
int fifo_pipe_getreadfd(fifo_pipe_t *pipe_p);
//write magic string to pipe from the main process to stop log process
void fifo_pipe_write_magic(fifo_pipe_t *pipe_p, char *send_buf);

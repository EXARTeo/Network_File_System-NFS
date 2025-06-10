#pragma once //#include one time only

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>


#include "sync_info_mem_store.h"
#include "common.h"


//Functions for writing the message format
void message_writer(int fd, char *buffer, bool terminal_output);
void worker_log_format(int fd, char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, char *operation, char *result, char *details);

//Functions for executing LIST-PULL-PUSH commands
int create_connection(char *host, int port);
void *worker(void *argv);
void add_sync_info(char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, buffer_t *sync_buffer, int worker_limit, int log_fd, int con_fd);
void buffer_cancel_sync(buffer_t *buffer, char *src, int log_fd, int con_fd);
#pragma once //#include one time only

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>

///////////////////////////////
//       All structures
///////////////////////////////

typedef struct sync_info{
    char *source_dir;
    char *source_host;
    int  source_port;

    char *target_dir;
    char *target_host;
    int  target_port;

    char *filename;
}sync_info;


typedef struct buffer_t{
    sync_info **item;
    int bufin;
    int bufout;
    int total_items;
    int bufsize;
    pthread_mutex_t buflock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
}buffer_t;


typedef struct worker_argv{
    buffer_t *buffer;
    int log_fd;
}worker_argv;


///////////////////////////////
//  All structures' functions
///////////////////////////////

//Functions for struct sync_info

sync_info *create_sync_info(char *source_dir, char *src_host, int src_port, char *target_dir, char *trg_host, int trg_port, char *filename);
void destroy_sync_info(sync_info *info);

//Functions for struct buffer_t

void init_buffer(buffer_t *buffer, int size);
void putitem(buffer_t *buffer, sync_info *new_item);
void getitem(buffer_t *buffer, sync_info **itemp);
void buffer_clean(buffer_t *buffer);
sync_info *find_sync_item(buffer_t *buffer, char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, char *filename);
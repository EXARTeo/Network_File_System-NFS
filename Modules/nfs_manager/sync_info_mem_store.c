#include "../../Includes/sync_info_mem_store.h"



///////////////////////////////////////////////////////////
//               sync_info struct fuctions               //
///////////////////////////////////////////////////////////


//Creates a new sync_info and returns the pointer to it
sync_info *create_sync_info(char *source_dir, char *src_host, int src_port, char *target_dir, char *trg_host, int trg_port, char *filename){
    sync_info *temp = malloc(sizeof(sync_info));
    if (temp == NULL) {
        perror("Failed to allocate memory for new sync_info");
        exit(EXIT_FAILURE);
    }
    temp->source_dir = strdup(source_dir);
    if (temp->source_dir == NULL) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    temp->source_host = strdup(src_host);
    if (temp->source_host == NULL) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    temp->target_dir = strdup(target_dir);
    if (temp->target_dir == NULL) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    temp->target_host = strdup(trg_host);
    if (temp->target_host == NULL) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    temp->filename = strdup(filename);
    if (temp->filename == NULL) {
        perror("strdup failed");
        exit(EXIT_FAILURE);
    }
    
    temp->source_port = src_port;
    temp->target_port = trg_port;

    return temp;
}

//Free the sync_info memory
void destroy_sync_info(sync_info *info){
    if (info == NULL)
        return;
    free(info->source_dir);
    free(info->source_host);
    free(info->target_dir);
    free(info->target_host);
    free(info->filename);
    free(info);
}



///////////////////////////////////////////////////////////
//                buffer_t struct fuctions               //
///////////////////////////////////////////////////////////


//Initialize the given buffer and sets the size limit 
void init_buffer(buffer_t *buffer, int size){
    buffer->item = malloc(size * sizeof(sync_info*));
    if (buffer->item == NULL){
        perror("malloc in buffer");
        exit(EXIT_FAILURE);
    }
    buffer->bufin = 0;
    buffer->bufout = 0;
    buffer->total_items = 0;
    buffer->bufsize = size;
    pthread_mutex_init(&buffer->buflock, NULL);
    pthread_cond_init(&buffer->not_empty, NULL);
    pthread_cond_init(&buffer->not_full, NULL);
}

//Inserts a new item into the given buffer..
//..waits until it can
void putitem(buffer_t *buffer, sync_info *new_item){
    if (pthread_mutex_lock(&buffer->buflock)) {
        perror("putitem() failed to pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    //Keep waiting untile there is space in the buffer to put the item
    while (!(buffer->total_items < buffer->bufsize)){
        pthread_cond_wait(&buffer->not_full, &buffer->buflock);
    }
    //When there is space continue inserting the new item

    buffer->item[buffer->bufin] = new_item;
    buffer->bufin = (buffer->bufin + 1) % buffer->bufsize;
    buffer->total_items++;

    pthread_cond_signal(&buffer->not_empty);    //Informs that there are items to get
    if (pthread_mutex_unlock(&buffer->buflock)) {
        perror("putitem() failed to pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
}

//Get the next item in line from the buffer..
//..waits until it can
void getitem(buffer_t *buffer, sync_info **itemp){
    if (pthread_mutex_lock(&buffer->buflock)){
        perror("getitem() failed to pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    //If there is no item to remove..
    while (buffer->total_items == 0){
        //..keep waiting here untile there is an item
        pthread_cond_wait(&buffer->not_empty, &buffer->buflock);
    }
    //When you found an item, get it and remove it

    *itemp = buffer->item[buffer->bufout];
    buffer->bufout = (buffer->bufout + 1) % buffer->bufsize;
    buffer->total_items--;

    pthread_cond_signal(&buffer->not_full);     //Informs that there is space to put items
    if (pthread_mutex_unlock(&buffer->buflock)){
        perror("getitem() failed to pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
}

//Clear everything in the given buffer
void buffer_clean(buffer_t *buffer){
    //Should be 0 memory leaks without it

    // for (int i = 0; i < buffer->bufsize; i++){
    //     destroy_sync_info(buffer->item[i]);
    // }

    free(buffer->item);
    pthread_mutex_destroy(&buffer->buflock);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_cond_destroy(&buffer->not_full);
}


//Returns a pointer to the buffer's sync item..
//..or NULL if it doesn't exist
sync_info *find_sync_item(buffer_t *buffer, char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, char *filename){
    if (pthread_mutex_lock(&buffer->buflock)) {
        perror("Failed to pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }
    
    for(int i = 0; i < buffer->total_items; i++){
        int j = (buffer->bufout + i) % buffer->bufsize;
        sync_info *temp = buffer->item[j];
        if (temp != NULL){
            if (strcmp(temp->filename, filename) == 0 &&
                strcmp(temp->source_dir, src) == 0 && strcmp(temp->source_host, src_host) == 0 && temp->source_port == src_port &&
                strcmp(temp->target_dir, trg) == 0 && strcmp(temp->target_host, trg_host) == 0 && temp->target_port == trg_port){

                if (pthread_mutex_unlock(&buffer->buflock)) {
                    perror("Failed to pthread_mutex_unlock");
                    exit(EXIT_FAILURE);
                }
                return temp;
            }
        }
    }
    if (pthread_mutex_unlock(&buffer->buflock)) {
        perror("Failed to pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
    return NULL;
}
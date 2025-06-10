#include "../../Includes/manager_utils.h"


/////////////////////////////////////////////////////////////
//        Functions for writing the message format         //
/////////////////////////////////////////////////////////////


//Print the full message format with one write ([TIMESTAMP] buffer)
void message_writer(int fd, char *buffer, bool terminal_output){

    time_t now = time(NULL);
    struct tm *time_0 = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_0);

    int len = snprintf(NULL, 0, "[%s] %s", time_str, buffer);
    char *output = malloc((len + 1) * sizeof(char));
    sprintf(output, "[%s] %s", time_str, buffer);

    //Output for fd
    if (fd > 0){
        //In case it is writing to a socket
        if(write_all(fd, output, len) < 0){
            perror("Writing to logfile failed");
        }
    }
    //Output for console
    if(terminal_output)
        printf("[%s] %s", time_str, buffer);

    free(output);
}

//Print the full message format with one write (including [TIMESTAMP] and [THREAD_PID])
void worker_log_format(int fd, char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, char *operation, char *result, char *details){
    time_t now = time(NULL);
    struct tm *time_0 = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_0);

    int len = snprintf(NULL, 0, "[%s] [%s@%s:%d] [%s@%s:%d] [%lu] [%s] [%s] [%s]\n", 
        time_str, src, src_host, src_port, trg, trg_host, trg_port, (unsigned long)pthread_self(), operation, result, details);
    char *log_output = malloc((len + 1) * sizeof(char));
    sprintf(log_output, "[%s] [%s@%s:%d] [%s@%s:%d] [%lu] [%s] [%s] [%s]\n", 
        time_str, src, src_host, src_port, trg, trg_host, trg_port, (unsigned long)pthread_self(), operation, result, details);
    write(fd, log_output, len);
    free(log_output);
}



/////////////////////////////////////////////////////////////
//       Functions executing LIST-PULL-PUSH commands       //
/////////////////////////////////////////////////////////////


//Creates a socket connection and returns the new socket..
//..or -1 if it failed
int create_connection(char *host, int port){
    int sock;
    struct sockaddr_in addr;
    struct sockaddr *ptr = (struct sockaddr*)&addr;
    struct hostent *rem;

    //Create the socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    if ((rem = gethostbyname(host)) == NULL) {
        close(sock);
        return -1;
    }
    memset(&addr, 0, sizeof(addr));         //Makes sure we clear old bytes
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, rem->h_addr, rem->h_length);
    addr.sin_port = htons(port);

    //Try to connect to the server..
    if (connect(sock, ptr, sizeof(addr)) < 0){
        close(sock);
        return -1;
    }

    return sock;    //..if succeed return the socket
}

//Making right use of clients' PULL-PUSH commands..
//..and informs with the right message format
void *worker(void *argv){
    worker_argv *worker_args = (worker_argv *)argv;
    buffer_t *buffer = worker_args->buffer;
    int log_fd       = worker_args->log_fd;

    while (true){
        sync_info *info;

        getitem(buffer, &info);
        if (info == NULL)   //AKA Shutdown info..
            break;          //..end the worker thread

        if ((info->source_dir[0] == '\0') && (info->filename[0] == '\0')){                  //Make sure that is not canceled
            destroy_sync_info(info);
            continue;
        }

        int src_len = snprintf(NULL, 0, "PULL %s/%s", info->source_dir, info->filename);    //-5 for source file len
        char *pull_comd = malloc((src_len + 1) * sizeof(char));                             //Saves <source file> PULL command
        sprintf(pull_comd, "PULL %s/%s", info->source_dir, info->filename);

        int trg_len = snprintf(NULL, 0, "PUSH %s/%s ", info->target_dir, info->filename);   //-6 for target file len
        char *push_comd = malloc((trg_len + 1) * sizeof(char));                             //Saves <target file> PUSH command
        sprintf(push_comd, "PUSH %s/%s ", info->target_dir, info->filename);

        //Prepare connection to ask "PULL" from <source> nfs_client
        int src_sock = create_connection(info->source_host, info->source_port);
        if (src_sock == -1){
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            continue;
        }

        //Send the PULL command to the <source> client
        pull_comd[src_len] = '\0';          //Makes sure that ends with "\0"
        if (write_all(src_sock, pull_comd, src_len + 1) == -1) {
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            close(src_sock);
            continue;
        }

        char filesize_str[21];
        long long filesize;
        char data[4096];    //Saves the data that will be pushed

        long long data_bytes = 0;
        while (true){
            ssize_t r_bytes = read(src_sock, filesize_str + data_bytes, sizeof(filesize_str) - data_bytes - 1);
            char *end;
            if((end = memchr(filesize_str, ' ', r_bytes + data_bytes)) != NULL){
                *end = '\0';
                filesize = atoll(filesize_str);
                int str_len = strlen(filesize_str) + 1;     //+1 for "space" char
                //If read already some data, save the size..
                data_bytes = (r_bytes + data_bytes) - str_len;
                //..and save them as the "first" <data>
                memcpy(data, filesize_str + str_len, data_bytes);
                break;
            }
            data_bytes += r_bytes;
        }

        if (filesize == -1){
            while(true){
                ssize_t r_bytes = read(src_sock, data + data_bytes, sizeof(data) - data_bytes - 1);
                if (r_bytes <= 0)
                    break;

                data_bytes += r_bytes;
            }
            //Just so it will be print inside the "]" of the format
            if (data[data_bytes - 1] == '\n')
                data[data_bytes - 1] = '\0';

            data[data_bytes] = '\0';    //Makes sures it ends with '\0'

            int details_len = snprintf(NULL, 0, "File: %s - %s", info->filename, data);
            char *details = malloc((details_len + 1) * sizeof(char));
            sprintf(details, "File: %s - %s", info->filename, data);

            worker_log_format(log_fd, info->source_dir, info->source_host, info->source_port, info->target_dir, info->source_host, info->source_port, "PULL", "ERROR", details);

            //Clear the memory..
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            close(src_sock);
            continue;       //..and go get next item
        }

        //Prepare connection to ask "PUSH" from <target> nfs_client
        int trg_sock = create_connection(info->target_host, info->target_port);
        if (trg_sock == -1){
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            close(src_sock);
            continue;
        }

        //Send the first PUSH command to truncate the target file
        if (write_all(trg_sock, push_comd, trg_len) == -1){
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            close(src_sock);
            close(trg_sock);
            continue;
        }
        //No need to send any data now, but the format needs space after the chunk_size
        if (write_all(trg_sock, "-1 ", 3) == -1){
            destroy_sync_info(info);
            free(pull_comd);
            free(push_comd);
            close(src_sock);
            close(trg_sock);
            continue;
        }

        //Send the "first" <data> if there are any
        if (data_bytes > 0){
            char chunk_str[21];
            ssize_t chunk_str_len = snprintf(chunk_str, sizeof(chunk_str), "%lld ", data_bytes);
            write_all(trg_sock, push_comd, trg_len);
            write_all(trg_sock, chunk_str, chunk_str_len);
            write_all(trg_sock, data, data_bytes);
        }

        //Keep PULLing the data from <source> and PUSHing them to <target>..
        //..until you push filesize bytes
        while (data_bytes < filesize){
            //Read as much data as you can
            ssize_t pull_bytes = read(src_sock, data, sizeof(data) - 1);
            if (pull_bytes == -1)
                break;

            data[pull_bytes] = '\0';

            char chunk_str[21];    //Save the expected size of this data chunk
            ssize_t chunk_str_len = snprintf(chunk_str, sizeof(chunk_str), "%zd ", pull_bytes);

            //Write the PUSH command..
            if (write_all(trg_sock, push_comd, trg_len) == -1)
                break;
            //..the chunk the client expects to be PUSH..
            if (write_all(trg_sock, chunk_str, chunk_str_len) == -1)
                break;
            //..and send all the <data> that have been pulled now
            if (write_all(trg_sock, data, pull_bytes) == -1)
                break;
            data_bytes += pull_bytes;   //Inform the total data that have been pushed
        }
        //Write the PUSH command..
        write_all(trg_sock, push_comd, trg_len);
        //..and tell client to stop
        write_all(trg_sock, "0 ", 2);

        char details[34];   //Biggest number 20 characters + 14 for the message

        sprintf(details, "%lld bytes pulled", data_bytes);
        worker_log_format(log_fd, info->source_dir, info->source_host, info->source_port, info->target_dir, info->source_host, info->source_port, "PULL", "SUCCESS", details);

        sprintf(details, "%lld bytes pushed", data_bytes);
        worker_log_format(log_fd, info->source_dir, info->source_host, info->source_port, info->target_dir, info->source_host, info->source_port, "PUSH", "SUCCESS", details);


        //Clear the memory
        destroy_sync_info(info);
        free(pull_comd);
        free(push_comd);
        close(src_sock);
        close(trg_sock);
    }
    pthread_exit(NULL);
}




//Starts the syncing of the <source_dir@source_host:source_port>..
//..and inform user with specific messages
void add_sync_info(char *src, char *src_host, int src_port, char *trg, char *trg_host, int trg_port, buffer_t *sync_buffer, int worker_limit, int log_fd, int con_fd){
    //Connecting to the nfs_client to get the files of the <source dir>
    //Start connection..
    int sock = create_connection(src_host, src_port);
    if (sock == -1)
        return;

    //..and send the "LIST" command
    char *list_command;
    int command_size = strlen(src) + 6;     //The lenght of the "LIST" + "space"+ "source_dir" + "\0"

    list_command = malloc(sizeof(char) * command_size);
    sprintf(list_command, "LIST %s", src);
    list_command[command_size - 1] = '\0';  //Makes sure there is "\0" at the end
    if (write_all(sock, list_command, command_size) < 0){
        free(list_command);
        close(sock);
        return;
    }

    //Getting ready to read from the socket
    char f_buffer[4096];
    ssize_t leftover_bytes = 0;
    bool flag = false;
    //Go through each line of the List and save each file
    while (true) {
        ssize_t bytes_read = read(sock, f_buffer + leftover_bytes, sizeof(f_buffer) - leftover_bytes - 1);
        if (bytes_read < 0){
            //If a signal stop the reading..
            if (errno == EINTR)
                continue;   //..just retry
            //Else true error case
            free(list_command);
            close(sock);
            return;
        }
        if (bytes_read == 0)
            break;

        //Finding the leght of the bytes we need at this read..
        int now_bytes = leftover_bytes + bytes_read;
        f_buffer[now_bytes] = '\0';     //..and make sure the f_buffer is treated as a "string"

        char *file_start = f_buffer;
        char *file_end;
        //As long there is a full file name (AKA '\n' in the end)..
        //..keep adding them and reading the buffer
        while ((file_end = strchr(file_start, '\n')) != NULL){
            *file_end = '\0';   //"Cut" the buffer, up to the end of this file name

            //Check if this is the end of the LIST
            if (strcmp(file_start, ".") == 0){
                flag = true;
                break;
            }

            //If the file already is waiting to be synced..
            if (find_sync_item(sync_buffer, src, src_host, src_port, trg, trg_host, trg_port, file_start) != NULL){
                //..inform the user with the right message and..
                int output_len = snprintf(NULL, 0, "Already in queue: %s/%s@%s:%d\n",
                    src, file_start, src_host, src_port);
                char *output = malloc((output_len + 1) * sizeof(char));
                sprintf(output, "Already in queue: %s/%s@%s:%d\n",
                    src, file_start, src_host, src_port);

                message_writer(con_fd, output, true);

                free(output);

                file_start = file_end + 1;
                continue;   //..just continue to check the next one
            }

            //Saving the info for the new <source_dir@source_host:source_port> <target_dir@target_host:target_port>..
            sync_info *new_info = create_sync_info(src, src_host, src_port, trg, trg_host, trg_port, file_start);
            //..and call a worker to execute the new task..
            //..or wait untile you put it in the sync_buffer to execute later by a worker
            putitem(sync_buffer, new_info);

            //Find the legth of the output
            int output_len = snprintf(NULL, 0, "Added file: %s/%s@%s:%d -> \n%s/%s@%s:%d\n",
                src, file_start, src_host, src_port, trg, file_start,  trg_host, trg_port);
            //And save it to print it
            char *output = malloc((output_len + 1) * sizeof(char));
            sprintf(output, "Added file: %s/%s@%s:%d -> \n%s/%s@%s:%d\n",
                src, file_start, src_host, src_port, trg, file_start,  trg_host, trg_port);

            message_writer(log_fd, output, true);
            message_writer(con_fd, output, false);
            free(output);

            //The new start of the file will be after the end of this one
            file_start = file_end + 1;
        }
        if (flag)   //If it is the end of the List..
            break;  //..stop the reading and return
        //If we end up reading from the socket again..
        leftover_bytes = strlen(file_start);            //..get the leftover bytes..
        memmove(f_buffer, file_start, leftover_bytes);  //..in the start of the buffer to form the full file name
    }

    close(sock);
    free(list_command);
}


//Marks all the items in the buffer with the given <source_dir>..
//..to later be skipped and delete from the worker (informs the user with messages)
void buffer_cancel_sync(buffer_t *buffer, char *src, int log_fd, int con_fd){
    if (pthread_mutex_lock(&buffer->buflock)) {
        perror("Failed to pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    char last_clear_host[256];  //Max length of host name is 255 characters
    last_clear_host[0] = '\0';
    int last_clear_port = -1;
    //Go through all the buffer..
    for(int i = 0; i < buffer->total_items; i++){
        int j = (buffer->bufout + i) % buffer->bufsize;
        sync_info *temp = buffer->item[j];
        //..and mark the item that has the same source as the given one
        if ((temp != NULL) && (strcmp(temp->source_dir, src) == 0)){
            //Make sure we print the message only once..
            if ((strcmp(temp->source_host, last_clear_host) != 0 ) || (temp->source_port != last_clear_port)){
                //..save the recent host and port..
                snprintf(last_clear_host, sizeof(last_clear_host), "%s", temp->source_host);
                last_clear_port = temp->source_port;
                //..write the message..
                int output_len = snprintf(NULL, 0, "Synchronization stopped for %s@%s:%d\n", temp->source_dir, temp->source_host, temp->source_port);
                char *output = malloc((output_len + 1) * sizeof(char));
                sprintf(output, "Synchronization stopped for %s@%s:%d\n", temp->source_dir, temp->source_host, temp->source_port);

                message_writer(log_fd, output, true);
                message_writer(con_fd, output, false);
                free(output);
            }
            //..and mark the sync info item
            temp->source_dir[0] = '\0';
            temp->filename[0] = '\0';
        }
    }
    //If no item was found with the given source, ..
    if (last_clear_port == -1) {
        //..inform with the right message
        int output_len = snprintf(NULL, 0, "Directory not being synchronized:\n%s\n", src);
        char *output = malloc((output_len + 1) * sizeof(char));
        sprintf(output, "Directory not being synchronized:\n%s\n", src);

        message_writer(con_fd, output, true);
        free(output);
    }

    if (pthread_mutex_unlock(&buffer->buflock)) {
        perror("Failed to pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
}

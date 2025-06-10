#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>

#include "../Includes/common.h"

void *exec_request(void *client_socket){
    //Get the socket..
    int sock = *(int *)client_socket;
    free(client_socket);    //..and free the memory right after

    //For PUSH command save the fd of the file to keep..
    //..writing with one open() in case of an other PUSH
    int trg_fd = -1;

    char buffer[4096];          //Will be used to read the socket info
    ssize_t leftover_bytes = 0;
    int command_left_b = 0;     //Max 5

    while(true){
        //Time to read the command (+ space character)
        ssize_t command_b = read(sock, buffer + leftover_bytes, 5 - command_left_b);
        leftover_bytes += command_b;
        command_left_b += command_b;

        if (memcmp(buffer, "LIST ", 5) == 0) {
            //Shift left 5 cells to "delete" the command and start with the data
            memmove(buffer, buffer + 5, sizeof(buffer) - 5);
            leftover_bytes -= 5;    //Don't forget to inform the len of the existed bytes

            while(true){
                ssize_t bytes_read = read(sock, buffer + leftover_bytes, sizeof(buffer) - leftover_bytes - 1);
                if (bytes_read <= 0){
                    //If a signal stop the reading..
                    if (errno == EINTR)
                        continue;   //..just retry
                    //Else true error case or EOF
                    break;
                }

                int now_bytes = leftover_bytes + bytes_read;
                char *file_start = buffer;
                char *file_end;
                //If we have the full command
                if((file_end = memchr(file_start, '\0', now_bytes)) != NULL){
                    DIR *source_dir = opendir(file_start);
                    if (source_dir == NULL) {           //If there is no such directory..
                        write_all(sock, ".\n", 2);      //..no error just treat it as an empty one..
                        break;                          //..and go end
                    }

                    struct dirent *dir_enrty;
                    char dir_file[4096];
                    //Keep getting entries from the directory
                    while ((dir_enrty = readdir(source_dir)) != NULL) {
                        if (dir_enrty->d_type == DT_REG) {      //Ignore the non-regular files
                            int len = sprintf(dir_file, "%s\n", dir_enrty->d_name);
                            write_all(sock, dir_file, len);
                        }
                    }
                    closedir(source_dir);
                    write_all(sock, ".\n", 2);
                }
                leftover_bytes = now_bytes;
            }
            break;
        }
        else if (memcmp(buffer, "PULL ", 5) == 0) {
            memmove(buffer, buffer + 5, sizeof(buffer) - 5);
            leftover_bytes -= 5;

            while(true){
                ssize_t bytes_read = read(sock, buffer + leftover_bytes, sizeof(buffer) - leftover_bytes - 1);
                if (bytes_read <= 0){
                    //If a signal stop the reading..
                    if (errno == EINTR)
                        continue;   //..just retry
                    //Else true error case or EOF
                    break;
                }
                int now_bytes = leftover_bytes + bytes_read;
                char *file_start = buffer;
                char *file_end;
                //If we have the full command
                if((file_end = memchr(file_start, '\0', now_bytes)) != NULL){
                    char data[4096];
                    struct stat fl_stats;
                    if (stat(file_start, &fl_stats) < 0){
                        //If the stat failed inform with the right error message..
                        int len = sprintf(data, "-1 %s\n", strerror(errno));
                        write_all(sock, data, len);
                        break;      //..and go end
                    }

                    int src_fd = open(file_start, O_RDONLY);
                    if (src_fd < 0) {
                        //If the open failed inform with the right error message..
                        int len = sprintf(data, "-1 %s\n", strerror(errno));
                        write_all(sock, data, len);
                        close(src_fd);
                        break;      //..and go end
                    }
                    //First, send the size of the file..
                    int len = sprintf(data, "%ld ", (long)fl_stats.st_size);
                    if (write_all(sock, data, len) == -1)
                        break;

                    ssize_t bytes_rd;
                    //..and then keep sending the data you reading, until EOF
                    while((bytes_rd = read(src_fd, data, sizeof(data))) > 0) {
                        write_all(sock, data, bytes_rd);
                    }
                    close(src_fd);
                    break;
                }
                leftover_bytes = now_bytes;
            }
            break;
        }
        else if (memcmp(buffer, "PUSH ", 5) == 0) {
            memmove(buffer, buffer + 5, sizeof(buffer) - 5);
            leftover_bytes -= 5;

            char *trg_file = NULL;
            char *chunk_size_str = NULL;
            long long chunk_size = -2;

            while(true){
                ssize_t bytes_read = read(sock, buffer + leftover_bytes, sizeof(buffer) - leftover_bytes - 1);
                if (bytes_read < 0){
                    //If a signal stop the reading..
                    if (errno == EINTR)
                        continue;   //..just retry
                    //Else true error case or EOF
                    break;
                }

                int now_bytes = leftover_bytes + bytes_read;
                char *start = buffer;
                char *end;
                //If there is no <target> file yet..
                if (trg_file == NULL) {
                    //..checks for the first "space" char..
                    if ((end = memchr(start, ' ', now_bytes)) != NULL) {
                        *end = '\0';                //..if there is, treat the start as a string..
                        trg_file = strdup(start);   //..and save the file name
                        *end = 'X';                 //Don't forget to put back a "non-space" character..
                        chunk_size_str = end;       //..to find chunk_size(AKA look for the second "space" char)
                        if (trg_file == NULL)
                            break;  //Go end if strdup failed
                    }
                }
                //If we have the start of the chunk size's str..
                //..(chunk_size_str = "X<chunk_size><space>"..
                //..where "X" is the end of the trg_file into the buffer)
                if (chunk_size_str != NULL) {
                    if ((end = memchr(chunk_size_str, ' ', now_bytes - strlen(trg_file))) != NULL) {
                        *end = '\0';                        //Don't forget -1 in the total buffer size later
                        chunk_size_str++;                   //Skip the 'X'
                        chunk_size = atoll(chunk_size_str);
                        now_bytes -= (strlen(buffer) + 1);  //strlen workes because of the end = '\0'
                        //Getting access direct to the data for now on
                        memmove(buffer, end + 1, now_bytes);
                    }
                }
                if(chunk_size != -2){
                    if (chunk_size == -1){
                        leftover_bytes = now_bytes;
                        //Open the file descriptor with the right format..
                        //..and overwrite existed data
                        trg_fd = open(trg_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (trg_fd < 0){
                            break;
                        }
                        break;  //Go back to the main loop to read the next PUSH command
                    }
                    else if (chunk_size > 0){
                        if (chunk_size > now_bytes){
                            long long end_of_chunk = chunk_size;
                            if (sizeof(buffer) < chunk_size)
                                chunk_size = sizeof(buffer);
                            ssize_t bytes_pushed = 0;
                            //Read until you read chunk_size data
                            while(bytes_pushed < end_of_chunk) {
                                ssize_t bytes_rd = read_all(sock, buffer + now_bytes, chunk_size - now_bytes);
                                now_bytes += bytes_rd;  //Infrom the new size of the buffer
                                //Write the <source> data to the <target> file
                                ssize_t now_pushed = write_all(trg_fd, buffer, now_bytes);
                                if (now_pushed == -1)
                                    break;
                                now_bytes -= now_pushed;
                                bytes_pushed += now_pushed;
                            }
                        }
                        else{
                            ssize_t now_pushed = write_all(trg_fd, buffer, chunk_size);
                            if (now_pushed == -1)
                                break;
                            now_bytes -= chunk_size;
                            memmove(buffer, buffer + chunk_size, now_bytes);
                        }

                        leftover_bytes = now_bytes;
                        break;  //Go back to the main loop to read the next PUSH command
                    }
                    else if (chunk_size == 0){
                        close(trg_fd);
                        break;  //Go back to end the main loop
                    }
                }
                //If the command is not complete (AKA didn't read 2 "spaces")..
                //..makes sure you read into the buffer after the existed bytes
                leftover_bytes = now_bytes;
            }
            command_left_b = 0;     //Getting ready for the next PUSH command

            if (trg_file != NULL)
                free(trg_file);

            //If the chunk size is 0..
            if (chunk_size == 0){
                //..no need to keep waiting for PUSH command..
                break;  //..go end
            }
        }
        else if (command_left_b == 5){
            break;  //Wrong command
        }
    }

    if (trg_fd > 0)
        close(trg_fd);

    close(sock);

    return NULL;
}



int main(int argc, char *argv[]) {

    if (argc != 3 || strcmp(argv[1], "-p") != 0){
        printf("Usage: %s -p <port_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port_number = atoi(argv[2]);
    if (port_number <= 0){
        printf("Usage: %s -p <port_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Creating the input socket
    int sock;       //This is the main socket, that the "nfs_client" using as a "server"
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    //The "nfs_client" act like a server..
    struct sockaddr_in server;              //..it will create the main socket to later connect to nfs_manager
    memset(&server, 0, sizeof(server));     //Makes sure we clear everything before using it
    struct sockaddr *serverptr = (struct sockaddr*)&server;

    server.sin_family = AF_INET;                    //Internet domain
    server.sin_addr.s_addr = htonl(INADDR_ANY);     //Any address
    server.sin_port = htons(port_number);           //The given port

    
    if (bind(sock, serverptr, sizeof(server)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 128) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    int accept_sock;            //The "server" socket conneting to "server" one
    struct sockaddr_in client;  //AKA the socket address to the nfs_manager 
    struct sockaddr *clientptr = (struct sockaddr *)&client;
    socklen_t clientlen;


    //Keep accepting request from manager
    while (true){
        clientlen = sizeof(client);
        memset(&client, 0, clientlen);      //Makes sure we clear everything before using it
        if ((accept_sock = accept(sock, clientptr, &clientlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        int *client_sock;
        //Allocate memory to save the accept socket..
        if ((client_sock = malloc(sizeof(int))) == NULL){
            close(accept_sock);
            continue;       //..if it failed, just retry
        }
        *client_sock = accept_sock;

        pthread_t request_th;
        if (pthread_create(&request_th, NULL, exec_request, client_sock) != 0) {
            perror("pthread_create");
            close(accept_sock);
            free(client_sock);
            continue;
        }
        pthread_detach(request_th);
    }

    close(sock);

    return 0;
}
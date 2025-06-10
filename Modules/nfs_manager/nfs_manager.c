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
#include <errno.h>
#include <time.h>
#include <signal.h>


#include "../../Includes/sync_info_mem_store.h"
#include "../../Includes/manager_utils.h"
#include "../../Includes/common.h"



int main(int argc, char *argv[]) {

    char *manager_logfile = NULL;
    char *config_file = NULL;
    int worker_limit = 5;       //Default value
    int port_number = -1;
    int bufferSize = -1;

    int opt;
    while ((opt = getopt(argc, argv, "l:c:n:p:b:")) != -1) {
        switch (opt) {
            case 'l':
                manager_logfile = optarg;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'n':
                worker_limit = atoi(optarg);
                break;
            case 'p':
                port_number = atoi(optarg);
                break;
            case 'b':
                bufferSize = atoi(optarg);
                break;
            default:
                //If there is a false argument, print error message
                fprintf(stderr, "Usage: %s -l <manager_logfile> -c <config_file> -n <worker_limit> -p <port_number> -b <bufferSize>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    //If there are not enough arguments, print error message
    if (manager_logfile == NULL || config_file == NULL || port_number <= 0 || bufferSize <= 0) {
        fprintf(stderr, "Not enough arguments or wrong input\nUsage: %s -l <manager_logfile> -c <config_file> -n <worker_limit> -p <port_number> -b <bufferSize>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (worker_limit <= 0)  //If value is not positive number..
        worker_limit = 5;   //..change to default value


    //Creating the input socket
    int server_sock;
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    //So that we can re-bind to it without TIME_WAIT problems
    int reuse_addr = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));     //Makes sure we clear everything before using it
    struct sockaddr *serverptr = (struct sockaddr*)&server;

    server.sin_family = AF_INET;                    //Internet domain
    server.sin_addr.s_addr = htonl(INADDR_ANY);     //Any address
    server.sin_port = htons(port_number);           //The given port

    if (bind(server_sock, serverptr, sizeof(server)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 128) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    //Getting ready the manager's logfile
    //Open for writing at end of file
    int managerlog_fd = open(manager_logfile, O_WRONLY | O_APPEND);
    if (managerlog_fd < 0) {
        perror("Open manager log file failed");
        exit(EXIT_FAILURE);
    }

    //Initialize the main buffer struct for the program
    buffer_t sync_buffer;
    init_buffer(&sync_buffer, bufferSize);

    //Getting ready the workers' args
    worker_argv worker_args;
    worker_args.buffer = &sync_buffer;
    worker_args.log_fd = managerlog_fd;

    //Creating Worker threads
    pthread_t *worker_threads;
    worker_threads = malloc(sizeof(pthread_t) * worker_limit);
    if (worker_threads == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < worker_limit; i++){
        if (pthread_create(&worker_threads[i], NULL, worker, &worker_args) != 0){
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }


    //Reading Config_file
    FILE *config_file_fp = fopen(config_file, "r");
    if (config_file_fp == NULL) {
        perror("Unable to open the file");
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read_len = 0;

    while ((read_len = getline(&line, &len, config_file_fp)) > 0) {
        //Makes the target directories clear of '\n' operator
        if(line[read_len - 1] == '\n')
            line[read_len - 1] = '\0';

        char *src          = strtok(line, "@");     //Saves the start of the <source_dir>
        char *src_host     = strtok(NULL, ":");     //Saves the start of the <source_host>
        char *src_port_str = strtok(NULL, " ");
        int src_port;
        if (src_port_str  != NULL){
            src_port       = atoi(src_port_str);    //Saves the number of the <source_port>
        }
        else{
            continue;
        }
        char *trg          = strtok(NULL, "@");     //Saves the start of the <target_dir>
        char *trg_host     = strtok(NULL, ":");     //Saves the start of the <target_dir>
        char *trg_port_str = strtok(NULL, " ");
        int trg_port;
        if (trg_port_str  != NULL){
            trg_port       = atoi(trg_port_str);    //Saves the number of the <target_port>
        }
        else{
            continue;
        }

        //Check that the format is right
        if ((src != NULL) && (src_host != NULL) && (src_port != 0) && 
            (trg != NULL) && (trg_host != NULL) && (trg_port != 0)){
            //Add and start syncing the <source_dir@source_host:source_port>
            add_sync_info(src, src_host, src_port, trg, trg_host, trg_port, &sync_buffer, worker_limit, managerlog_fd, -1);
        }
    }
    free(line);
    fclose(config_file_fp);



    //Getting ready to receive and accept connections
    int console_sock;
    struct sockaddr_in console;
    struct sockaddr *consoleptr = (struct sockaddr*)&console;
    socklen_t console_len;

    console_len = sizeof(console);
    memset(&console, 0, console_len);       //Makes sure we clear everything before the new accept

    //Accept the connection
    if ((console_sock = accept(server_sock, consoleptr, &console_len)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    while (true) {
        char console_input[4096];
        ssize_t now_bytes = 0;
        //Read untile you get the '\0' char from console
        while(true){
            ssize_t r_bytes = read(console_sock, console_input + now_bytes, sizeof(console_input) - now_bytes);
            now_bytes += r_bytes;
            if (console_input[now_bytes - 1] == '\0')
                break;
        }

        if (strcmp(console_input, "shutdown") == 0) {
            message_writer(console_sock, "Shutting down manager...\n", true);
            break;      //Let's end the madness!!!
        }

        //Find the command argument
        char *command = strtok(console_input, " ");
        if (command == NULL) {       //Wrong format..
            continue;                //..skip this command
        }

        if (strncmp(command, "add", 3) == 0) {
            char *src          = strtok(NULL, "@");
            char *src_host     = strtok(NULL, ":");
            char *src_port_str = strtok(NULL, " ");
            int src_port;
            if (src_port_str  != NULL){
                src_port       = atoi(src_port_str);
            }
            else{
                write_all(console_sock, "\0", 1);
                continue;
            }
            char *trg          = strtok(NULL, "@");
            char *trg_host     = strtok(NULL, ":");
            char *trg_port_str = strtok(NULL, " ");
            int trg_port;
            if (trg_port_str  != NULL){
                trg_port       = atoi(trg_port_str);
            }
            else{
                write_all(console_sock, "\0", 1);
                continue;
            }

            //Check that the format is right
            if ((src != NULL) && (src_host != NULL) && (src_port != 0) && 
                (trg != NULL) && (trg_host != NULL) && (trg_port != 0)){
                //Add and start syncing the <source_dir@source_host:source_port>
                add_sync_info(src, src_host, src_port, trg, trg_host, trg_port, &sync_buffer, worker_limit, managerlog_fd, console_sock);
            }
            //Inform the console not to wait for other output
            if (write_all(console_sock, "\0", 1) != 1)
                exit(EXIT_FAILURE);

        }
        else if (strncmp(command, "cancel", 6) == 0) {
            char *src = strtok(NULL, "");
            //Check that the format is right
            if (src != NULL){
                //Clear the buffer from items with the given <source dir>
                buffer_cancel_sync(&sync_buffer, src, managerlog_fd, console_sock);

            }
            //Inform the console not to wait for an output
            if (write_all(console_sock, "\0", 1) != 1)
                exit(EXIT_FAILURE);

        }
        else{
            //If there is no such a command,..
            //..inform the console not to wait for an output
            if (write_all(console_sock, "\0", 1) != 1)
                exit(EXIT_FAILURE);
        }
    }

    //Time to tell each worker that there will be no more items..
    for (int i = 0; i < worker_limit; i++){
        //..so, a worker will stop, when they found a NULL item
        putitem(&sync_buffer, NULL);
    }

    message_writer(console_sock, "Waiting for all active workers to finish.\n", true);

    for (int i = 0; i < worker_limit; i++){
        pthread_join(worker_threads[i], NULL);
    }
    free(worker_threads);
    buffer_clean(&sync_buffer); 

    message_writer(console_sock, "Processing remaining queued tasks.\n", true);
    message_writer(console_sock, "Manager shutdown complete.\n", true);

    return 0;
}
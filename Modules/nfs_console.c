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

#include "../Includes/common.h"

void time_writer(int log_fd, bool terminal_output){
    time_t now = time(NULL);
    struct tm *time_0 = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_0);
    //Output for logfile
    write(log_fd, "[", 1);
    write(log_fd, time_str, strlen(time_str));
    write(log_fd, "] ", 2);
    //Output for console
    if(terminal_output)
        printf("[%s] ", time_str);
}

void message_writer(int log_fd, char *buffer, bool terminal_output){
    //Output for logfile
    if(write(log_fd, buffer, strlen(buffer)) < 0){
        perror("Writing to logfile failed");
    }
    //Output for console
    if(terminal_output)
        printf("%s", buffer);
}


//Returns true when command has valid format
bool write_command (char* command, int consolelog_fd){
    //Count the spaces (' ') in the command
    int count = 0;
    int arrow_iter = 0;
    for(int i = 0; command[i] != '\0'; i++){
        if(command[i] == ' '){
            count++;
        }
        else if (count == 2 && arrow_iter == 0){
            arrow_iter = i;
        }
    }
    if (strncmp(command, "add", 3) == 0 && count == 2){
        //Write the new command into the log file too
        time_writer(consolelog_fd, false);
        message_writer(consolelog_fd, "Command ", false);
        char temp = command[arrow_iter];
        command[arrow_iter] = '\0';
        message_writer(consolelog_fd, command, false);
        message_writer(consolelog_fd, "-> ", false);
        command[arrow_iter] = temp;
        message_writer(consolelog_fd, &command[arrow_iter], false);
        message_writer(consolelog_fd, "\n", false);
        return true;
    }
    else if (((strncmp(command, "cancel", 6) == 0) && (count == 1)) || (strcmp(command, "shutdown") == 0)){
        time_writer(consolelog_fd, false);
        message_writer(consolelog_fd, "Command ", false);
        message_writer(consolelog_fd, command, false);
        message_writer(consolelog_fd, "\n", false);
        return true;
    }
    //Unknown command
    return false;
}


int main(int argc, char *argv[]) {

    if (argc != 7 || (strcmp(argv[1], "-l") != 0) || (strcmp(argv[3], "-h") != 0) || (strcmp(argv[5], "-p") != 0)){
        printf("Usage: %s -l <console-logfile> -h <host_IP> -p <host_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Getting access..
    char *console_logfile = argv[2];
    //..and open the console logfile
    int consolelog_fd = open(console_logfile, O_WRONLY | O_APPEND);
    if (consolelog_fd < 0) {
        perror("Open console log file failed");
        exit(EXIT_FAILURE);
    }

    //Getting the host info
    char *host_ip = argv[4];
    int host_port = atoi(argv[6]);

    //Prepare the socket connection to the manager
    int sock;
    struct sockaddr_in sock_addr;
    struct sockaddr *sock_ptr = (struct sockaddr*)&sock_addr;
    struct hostent *rem;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    if ((rem = gethostbyname(host_ip)) == NULL) {
        herror("gethostbyname");
        close(sock);
        exit(EXIT_FAILURE);
    }
    memset(&sock_addr, 0, sizeof(sock_addr));     //Makes sure we clear old bytes
    sock_addr.sin_family = AF_INET;
    memcpy(&sock_addr.sin_addr, rem->h_addr, rem->h_length);
    sock_addr.sin_port = htons(host_port);

    //Connect to the nfs_manager
    if (connect(sock, sock_ptr, sizeof(sock_addr)) < 0){
        perror("connect");
        close(sock);
        exit(EXIT_FAILURE);
    }


    //Get user input
    char *command = NULL;
    size_t len = 0;
    while(true){
        printf("> ");
        fflush(stdout);     //Makes sure the message gets print immediately

        ssize_t read_len = getline(&command, &len, stdin);
        if (read_len < 0){
            break;          //In case of EOF or other error end the process
        }

        //If the string was read through terminal..
        //..makes sure to chop the '\n'
        if(command[read_len - 1] == '\n')
            command[read_len - 1] = '\0';

        //If the command is invalid..
        if (!write_command(command, consolelog_fd)){
            continue;   //..skip the command
        }

        //Send the command to the manager
        if (write_all(sock, command, read_len) < read_len){
            perror("Error writing into the host's socket");
            free(command);
            close(sock);
            close(consolelog_fd);
            exit(EXIT_FAILURE);
        }

        //Receive manager's response
        while (true) {
            char buffer[256];
            //Get the message (or a part of it)
            ssize_t bytes_read = read(sock, buffer, 256);
            if (bytes_read == -1) {
                //If a signal stop the reading..
                if (errno == EINTR)
                    continue;   //..just retry
                //Else true error case
                free(command);
                close(sock);
                close(consolelog_fd);
                perror("Error reading from host's socket\n");
                exit(EXIT_FAILURE);
            }   //If you found EOF, stop
            else if(bytes_read == 0){
                break;
            }
            //Flag to know if we got full output with this read();
            bool EOF_flag = buffer[bytes_read - 1] == '\0';
            ssize_t limit_EOF = bytes_read;
            if (EOF_flag)
                limit_EOF--;

            //Writing into the console log file
            if (write(consolelog_fd, buffer, limit_EOF) < 0) {
                perror("Error writing to logfile");
                free(command);
                close(sock);
                close(consolelog_fd);
                exit(EXIT_FAILURE);
            }
            //Printing to the terminal
            if (write(STDOUT_FILENO, buffer, limit_EOF) < 0) {
                perror("Error writing to terminal");
                free(command);
                close(sock);
                close(consolelog_fd);
                exit(EXIT_FAILURE);
            }
            fflush(stdout);

            //If you found the NULL terminator on last byte..
            if (buffer[bytes_read - 1] == '\0')
                break;      //..stop reading/writing
        }
        
        //Check if the command was shutdown!
        if (strncmp(command, "shutdown", 8) == 0)
            break;
    }
    free(command);
    close(sock);
    close(consolelog_fd);
    
    return 0;
}
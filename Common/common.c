#include "../Includes/common.h"



//Keeps writing to the fd until..
//..all buffer's bytes are written(-1 on error)
int write_all(int fd, char *buffer, size_t size){
    size_t sent, n;
    for (sent = 0; sent < size; sent += n) {
        if ((n = write(fd, buffer + sent, size - sent)) == -1)
            return -1;
    }
    return sent;
}


//Keeps reading from the fd until..
//..all buffer gets size bytes(-1 on error)
int read_all(int fd, char *buffer, size_t size){
    size_t i = 0;
    while(i < size){
        ssize_t read_bytes = read(fd, buffer + i, size - i);
        if (read_bytes <= 0)
            return -1;
        i += read_bytes;
    }
    return i;
}
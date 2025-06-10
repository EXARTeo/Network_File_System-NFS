#pragma once //#include one time only

#include <stdio.h>
#include <unistd.h>



int read_all(int fd, char *buffer, size_t size);
int write_all(int fd, char *buffer, size_t size);

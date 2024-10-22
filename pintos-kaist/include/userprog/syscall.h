#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/off_t.h"

void syscall_init (void);
void close(int fd);
void exit(int status);


#endif /* userprog/syscall.h */

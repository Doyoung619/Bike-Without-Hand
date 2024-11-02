#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"
#include "filesys/off_t.h"

void syscall_init (void);

void check_address (void *addr);
struct lock syscall_lock;
int g_fd_nr;

typedef int pid_t;

struct fd_list_elem {
    int fd;
    struct list_elem elem;
    struct file *file_ptr;
};

void halt (void);
void exit (int status);
pid_t fork (const char *thread_name);

int exec (const char *cmd_line);
int wait (pid_t pid);

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);

void seek (int fd, unsigned position);

unsigned tell (int fd);
void close (int fd);


#endif /* userprog/syscall.h */

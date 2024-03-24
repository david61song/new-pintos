#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stddef.h>

void syscall_init (void);
void halt(void);
size_t write(int fildes, const void *buf, size_t nbyte);



#endif /* userprog/syscall.h */

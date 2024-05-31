#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static int64_t get_user(const uint8_t *);
static bool put_user(uint8_t *, uint8_t);
void syscall_init (void);
void sys_halt(void);
size_t sys_write(int fildes, const void *buf, size_t nbyte);
void sys_exit(int);
int sys_open(const char *);


#endif /* userprog/syscall.h */

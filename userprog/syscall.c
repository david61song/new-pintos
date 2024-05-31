#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "include/lib/syscall-nr.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below KERN_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */


/* System Call function implementation */

void
sys_exit(int status){
	struct thread *curr = thread_current();
	curr -> exit_code = status;
	thread_exit();
}

void
sys_halt(void){
	power_off();
}

size_t
sys_write(int fildes, const void *buf, size_t nbyte){
	if (fildes == 1)
		printf("%s", (char *)buf);
	
	return sizeof(buf);
}


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

int
sys_open(const char* path){	
	/* check if path is not NULL */
	if (path == NULL){
	    return -1;
	}

	/* check if path pointer is below KERN_BASE */
	if (is_user_vaddr(path) == 0){
	    return -1;
	}

	struct thread *curr = thread_current();
	struct file *file_p = filesys_open(path);


	/* check if path pointer is valid */
	if (get_user((const uint8_t *)path) == -1){
	    return -1;
	}

	/* check if file does not exists in our file system */
	if (file_p == NULL){
	    return -1;
	}
	curr->filedes_table[curr->filedes_top] = file_p;
	curr->filedes_top ++;

	return (curr->filedes_top) - 1;
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	uint64_t syscall_num;
	syscall_num = f -> R.rax;

	switch (syscall_num){
	    case SYS_HALT:
			sys_halt();
			break;
	    case SYS_EXIT:
			sys_exit(f->R.rdi);
			break;
	    case SYS_FORK:
			break;
	    case SYS_EXEC:
			break;
	    case SYS_WAIT:
			break;
	    case SYS_CREATE:
			break;
	    case SYS_REMOVE:
			break;
	    case SYS_OPEN:
			f->R.rax = sys_open((const char *)f->R.rdi);
	    case SYS_FILESIZE:
			break;
	    case SYS_READ:
			break;
	    case SYS_WRITE:
			f->R.rax = sys_write(f->R.rdi, (void *)f->R.rsi, f->R.rdx);
			break;
	    case SYS_SEEK:
			break;
	    case SYS_TELL:
			break;
	    case SYS_CLOSE:
			break;

	    default :
			sys_halt();
		    break;
	}

}

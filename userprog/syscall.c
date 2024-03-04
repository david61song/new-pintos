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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt();

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

void halt(){
	power_off();
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



/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	int syscall_num;
	syscall_num = f -> R.rax;

	switch (syscall_num){
	    case SYS_HALT:
		halt();
		printf("HALT!!!");
		break;
	    case SYS_EXIT:
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
		break;
	    case SYS_FILESIZE:
		break;
	    case SYS_READ:
		break;
	    case SYS_WRITE:
		break;
	    case SYS_SEEK:
		break;
	    case SYS_TELL:
		break;
	    case SYS_CLOSE:
		break;


	    default :
		    printf("default!!");
		    printf("NUM IS %d! \n", syscall_num);
		    break;
	}

	thread_exit ();
}

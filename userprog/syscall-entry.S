#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function

/*
 * syscall_entry
 * This label performs the following tasks at the system call entry point:
 * 
 * 1. Saves some register values temporarily.
 * 2. Stores the user mode stack pointer.
 * 3. Retrieves the kernel stack pointer from the TSS and switches the stack.
 * 4. Constructs an interrupt frame, which includes information needed to return to user mode later:
 *    - Segment selectors (SS, CS, DS, ES)
 *    - Stack pointer (RSP)
 *    - Program counter (RIP)
 *    - Flags register (EFLAGS)
 *    - Other general-purpose registers
 * 5. Sets the address of the constructed interrupt frame as the first argument.
 * 
 * This process ensures a safe transition from user mode to kernel mode, preserves all necessary
 * information for returning to user mode later, and prepares for handling the system call in kernel mode.
 */


syscall_entry:
    movq %rbx, temp1(%rip)     /* Save rbx to temporary storage temp1 */
    movq %r12, temp2(%rip)     /* Save r12 to temporary storage temp2 (callee-saved register) */
    movq %rsp, %rbx            /* Store user mode stack pointer in rbx */
    movabs $tss, %r12          /* Load address of TSS into r12 */
    movq (%r12), %r12          /* Load first 8 bytes of TSS structure into r12 */
    movq 4(%r12), %rsp         /* Load kernel stack pointer (RSP0) from TSS into rsp */
    /* Now we've switched to the kernel stack */

    /* Start building the interrupt frame */
    push $(SEL_UDSEG)          /* Push user data segment selector (SS) */
    push %rbx                  /* Push user mode stack pointer (RSP) */
    push %r11                  /* Push EFLAGS */
    push $(SEL_UCSEG)          /* Push user code segment selector (CS) */
    push %rcx                  /* Push return address (RIP) */
    subq $16, %rsp             /* Reserve space for error_code and vec_no */
    push $(SEL_UDSEG)          /* Push DS */
    push $(SEL_UDSEG)          /* Push ES */
    push %rax                  /* Push RAX */
    movq temp1(%rip), %rbx     /* Restore saved RBX value */
    push %rbx                  /* Push RBX */
    pushq $0                   /* Push 0 instead of RCX (RCX already used for RIP) */
    push %rdx                  /* Push RDX */
    push %rbp                  /* Push RBP */
    push %rdi                  /* Push RDI */
    push %rsi                  /* Push RSI */
    push %r8                   /* Push R8 */
    push %r9                   /* Push R9 */
    push %r10                  /* Push R10 */
    pushq $0                   /* Push 0 instead of R11 (R11 already used for EFLAGS) */
    movq temp2(%rip), %r12     /* Restore saved R12 value */
    push %r12                  /* Push R12 */
    push %r13                  /* Push R13 */
    push %r14                  /* Push R14 */
    push %r15                  /* Push R15 */
    movq %rsp, %rdi            /* Set current stack pointer as first argument */


check_intr:
	btsq $9, %r11          /* Check whether we recover the interrupt */
	jnb no_sti
	sti                    /* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* if->rip */
	addq $8, %rsp
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0

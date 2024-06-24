#include "userprog/gdt.h"
#include <debug.h>
#include "userprog/tss.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

/* The Global Descriptor Table (GDT).
 *
 * The GDT, an x86-64 specific structure, defines segments that can
 * potentially be used by all processes in a system, subject to
 * their permissions.  There is also a per-process Local
 * Descriptor Table (LDT) but that is not used by modern
 * operating systems.
 *
 * Each entry in the GDT, which is known by its byte offset in
 * the table, identifies a segment.  For our purposes only three
 * types of segments are of interest: code, data, and TSS or
 * Task-State Segment descriptors.  The former two types are
 * exactly what they sound like.  The TSS is used primarily for
 * stack switching on interrupts. */

/*
 * In 64-bit mode, the Base and Limit values are ignored, 
 * each descriptor covers the entire linear address space regardless of 
 * what they are set to.
 * For more information, see Section 3.4.5: 
 * Segment Descriptors and Figure 3-8:
 * Segment Descriptor of the Intel Software Developer Manual, Volume 3-A.
 */

/*
 * Global Descriptor Table (GDT) initial setup:
 *
 * 1. NULL Descriptor (index 0):
 *    - All fields set to 0
 *
 * 2. Kernel Code Segment (SEL_KCSEG, index 1):
 *    - Type: 0xa (Execute/Read, Code)
 *    - Base: 0x0
 *    - Limit: 0xffffffff
 *    - DPL (Descriptor Privilege Level): 0 (Kernel mode)
 *
 * 3. Kernel Data Segment (SEL_KDSEG, index 2):
 *    - Type: 0x2 (Read/Write, Data)
 *    - Base: 0x0
 *    - Limit: 0xffffffff
 *    - DPL: 0 (Kernel mode)
 *
 * 4. User Data Segment (SEL_UDSEG, index 3):
 *    - Type: 0x2 (Read/Write, Data)
 *    - Base: 0x0
 *    - Limit: 0xffffffff
 *    - DPL: 3 (User mode)
 *
 * 5. User Code Segment (SEL_UCSEG, index 4):
 *    - Type: 0xa (Execute/Read, Code)
 *    - Base: 0x0
 *    - Limit: 0xffffffff
 *    - DPL: 3 (User mode)
 *
 * 6. TSS (Task State Segment) Descriptor (SEL_TSS, index 5):
 *    - Initially all fields set to 0, later filled in gdt_init()
 *
 * 7. Two additional empty entries (indices 6 and 7):
 *    - All fields set to 0
 *
 * The SEG64 macro is used to create these entries, which sets up 64-bit segment descriptors.
 * Notable characteristics:
 * - All segments have a base of 0 and a limit of 0xffffffff (4GB)
 * - The 'L' bit is set (value 1), indicating 64-bit mode
 * - The 'G' bit is set, meaning the limit is scaled by 4KB
 * - Kernel segments have DPL 0, user segments have DPL 3
 *
 * The TSS descriptor is initially empty and is properly set up later in the gdt_init() function,
 * where it's configured to point to the actual Task State Segment structure.
 *
 * This GDT setup provides the basic segmentation model required for the operating system,
 * distinguishing between kernel and user mode, and providing necessary segments for code and data
 * in both privilege levels.
 */

/*
 * Segment Descriptor Structure (figure) (segment_desc)
 *
 *  31                 24 23    22    21    20 19      16 15 14    13 12     11   8      7    0 [+32]
 * +---------------------+-----+-----+-----+---+--------+--+-------+--------+--------+---------+
 * |       Base Address  | G   | D/B | L   |AVL|  Limit  |P |  DPL  |   S    | Type   | Base   |
 * |        [31:24]      |     |     |     |   | [19:16] |  |       |        |        | Address|
 * +---------------------+-----+-----+-----+---+--------+--+-------+--------+--------+---------+
 *  31                                      16 15                                         0
 * +------------------------------------------+---------------------------------------------+
 * |                 Base Address             |                 Segment Limit               |
 * |                   [23:16]                |                  [15:00]                    |
 * +------------------------------------------+---------------------------------------------+
 */



struct segment_desc {
	unsigned lim_15_0 : 16;
	unsigned base_15_0 : 16;
	unsigned base_23_16 : 8;
	unsigned type : 4;
	unsigned s : 1;
	unsigned dpl : 2;
	unsigned p : 1;
	unsigned lim_19_16 : 4;
	unsigned avl : 1;
	unsigned l : 1;
	unsigned db : 1;
	unsigned g : 1;
	unsigned base_31_24 : 8;
};

struct segment_descriptor64 {
	unsigned lim_15_0 : 16;
	unsigned base_15_0 : 16;
	unsigned base_23_16 : 8;
	unsigned type : 4;
	unsigned s : 1;
	unsigned dpl : 2;
	unsigned p : 1;
	unsigned lim_19_16 : 4;
	unsigned avl : 1;
	unsigned rsv1 : 2;
	unsigned g : 1;
	unsigned base_31_24 : 8;
	uint32_t base_63_32;
	unsigned res1 : 8;
	unsigned clear : 8;
	unsigned res2 : 16;
};

#define SEG64(type, base, lim, dpl) (struct segment_desc) \
{ ((lim) >> 12) & 0xffff, (base) & 0xffff, ((base) >> 16) & 0xff, \
	type, 1, dpl, 1, (unsigned) (lim) >> 28, 0, 1, 0, 1, \
	(unsigned) (base) >> 24 }

static struct segment_desc gdt[SEL_CNT] = {
	[SEL_NULL >> 3] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	[SEL_KCSEG >> 3] = SEG64 (0xa, 0x0, 0xffffffff, 0),
	[SEL_KDSEG >> 3] = SEG64 (0x2, 0x0, 0xffffffff, 0),
	[SEL_UDSEG >> 3] = SEG64 (0x2, 0x0, 0xffffffff, 3),
	[SEL_UCSEG >> 3] = SEG64 (0xa, 0x0, 0xffffffff, 3),
	[SEL_TSS >> 3] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	[6] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	[7] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

struct desc_ptr gdt_ds = {
	.size = sizeof(gdt) - 1,
	.address = (uint64_t) gdt
};

/* Sets up a proper GDT.  The bootstrap loader's GDT didn't
   include user-mode selectors or a TSS, but we need both now. */


void
gdt_init (void) {
	/* Initialize GDT. */
	struct segment_descriptor64 *tss_desc =
		(struct segment_descriptor64 *) &gdt[SEL_TSS >> 3];
	struct task_state *tss = tss_get ();

	*tss_desc = (struct segment_descriptor64) {
		.lim_15_0 = (uint64_t) (sizeof (struct task_state)) & 0xffff,
		.base_15_0 = (uint64_t) (tss) & 0xffff,
		.base_23_16 = ((uint64_t) (tss) >> 16) & 0xff,
		.type = 0x9,
		.s = 0,
		.dpl = 0,
		.p = 1,
		.lim_19_16 = ((uint64_t)(sizeof (struct task_state)) >> 16) & 0xf,
		.avl = 0,
		.rsv1 = 0,
		.g = 0,
		.base_31_24 = ((uint64_t)(tss) >> 24) & 0xff,
		.base_63_32 = ((uint64_t)(tss) >> 32) & 0xffffffff,
		.res1 = 0,
		.clear = 0,
		.res2 = 0
	};

	lgdt (&gdt_ds);
	/* reload segment registers */
	asm volatile("movw %%ax, %%gs" :: "a" (SEL_UDSEG));
	asm volatile("movw %%ax, %%fs" :: "a" (0));
	asm volatile("movw %%ax, %%es" :: "a" (SEL_KDSEG));
	asm volatile("movw %%ax, %%ds" :: "a" (SEL_KDSEG));
	asm volatile("movw %%ax, %%ss" :: "a" (SEL_KDSEG));
	asm volatile("pushq %%rbx\n"
			"movabs $1f, %%rax\n"
			"pushq %%rax\n"
			"lretq\n"
			"1:\n" :: "b" (SEL_KCSEG):"cc","memory");
	/* Kill the local descriptor table */
	lldt (0);
}

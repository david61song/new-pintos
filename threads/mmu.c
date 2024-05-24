#include <stdbool.h>                 // Define boolean data type
#include <stddef.h>                  // Define size_t and NULL
#include <string.h>                  // Provide string manipulation functions
#include "threads/init.h"            // Thread initialization functions
#include "threads/pte.h"             // Page Table Entry definitions
#include "threads/palloc.h"          // Physical memory allocation functions
#include "threads/thread.h"          // Thread management functions
#include "threads/mmu.h"             // Memory Management Unit definitions
#include "intrinsic.h"               // Intrinsic functions

/* Walks through the page directory pointed to by pdp to return the page table entry 
 * corresponding to the virtual address va. If create is true, missing page table pages are created.
 */
static uint64_t *
pgdir_walk (uint64_t *pdp, const uint64_t va, int create) {
	int idx = PDX (va);                                                      // Get directory index from virtual address
	if (pdp) {                                                               // If page directory is not NULL
		uint64_t *pte = (uint64_t *) pdp[idx];                               // Get the page table entry
		if (!((uint64_t) pte & PTE_P)) {                                     // If the page table entry is not present
			if (create) {                                                    // If create flag is true
				uint64_t *new_page = palloc_get_page (PAL_ZERO);             // Allocate a new page frame with zeroed contents
				if (new_page)
					pdp[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;      // Set the page table entry with frame address and flags
				else
					return NULL;                                             // Return NULL if page allocation failed
			} else
				return NULL;                                                 // Return NULL if not creating
		}
		return (uint64_t *) ptov (PTE_ADDR (pdp[idx]) + 8 * PTX (va));       // Return the address of the resultant page table entry
	}
	return NULL;                                                             // Return NULL if the page directory is NULL
}

/* Walks through the page directory pointer table pointed to by pdpe to return the page table entry
 * corresponding to the virtual address va. If create is true, missing directory pages are created.
 */
static uint64_t *
pdpe_walk (uint64_t *pdpe, const uint64_t va, int create) {
	uint64_t *pte = NULL;                                                    // Initialize page table entry pointer as NULL
	int idx = PDPE (va);                                                     // Get directory pointer index from virtual address
	int allocated = 0;                                                       // Track if a new page was allocated
	if (pdpe) {                                                              // If page directory pointer is not NULL
		uint64_t *pde = (uint64_t *) pdpe[idx];                              // Get the page directory entry
		if (!((uint64_t) pde & PTE_P)) {                                     // If the page directory entry is not present
			if (create) {                                                    // If create flag is true
				uint64_t *new_page = palloc_get_page (PAL_ZERO);             // Allocate a new zeroed page frame
				if (new_page) {                                              // If page allocation succeeds
					pdpe[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;     // Set the directory entry with frame address and flags
					allocated = 1;                                           // Mark that a new page was allocated
				} else
					return NULL;                                             // Return NULL if page allocation fails
			} else
				return NULL;                                                 // Return NULL if not creating
		}
		pte = pgdir_walk (ptov (PTE_ADDR (pdpe[idx])), va, create);          // Walk down to the page directory level
	}
	if (pte == NULL && allocated) {                                          // If page table entry is NULL and a new page was allocated
		palloc_free_page ((void *) ptov (PTE_ADDR (pdpe[idx])));             // Free the allocated page
		pdpe[idx] = 0;                                                       // Clear the directory entry
	}
	return pte;                                                              // Return the page table entry
}

/* Returns the address of the page table entry for virtual address va in page map level 4 (PML4).
 * If the PML4 entry does not have a page table for va and create is true, a new page table is created and a pointer is returned.
 * Otherwise, a null pointer is returned.
 */
uint64_t *
pml4e_walk (uint64_t *pml4e, const uint64_t va, int create) {
	uint64_t *pte = NULL;                                                    // Initialize page table entry pointer as NULL
	int idx = PML4 (va);                                                     // Get PML4 index from virtual address
	int allocated = 0;                                                       // Track if a new page was allocated
	if (pml4e) {                                                             // If PML4 is not NULL
		uint64_t *pdpe = (uint64_t *) pml4e[idx];                            // Get the page directory pointer entry
		if (!((uint64_t) pdpe & PTE_P)) {                                    // If the directory entry is not present
			if (create) {                                                    // If create flag is true
				uint64_t *new_page = palloc_get_page (PAL_ZERO);             // Allocate a new zeroed page frame
				if (new_page) {                                              // If page allocation succeeds
					pml4e[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;    // Set the PML4 entry with frame address and flags
					allocated = 1;                                           // Mark that a new page was allocated
				} else
					return NULL;                                             // Return NULL if page allocation fails
			} else
				return NULL;                                                 // Return NULL if not creating
		}
		pte = pdpe_walk (ptov (PTE_ADDR (pml4e[idx])), va, create);          // Walk down to the directory pointer table level
	}
	if (pte == NULL && allocated) {                                          // If page table entry is NULL and a new page was allocated
		palloc_free_page ((void *) ptov (PTE_ADDR (pml4e[idx])));            // Free the allocated page
		pml4e[idx] = 0;                                                      // Clear the PML4 entry
	}
	return pte;                                                              // Return the page table entry
}

/* Creates and returns a new page map level 4 (PML4) with mappings for kernel virtual addresses,
 * but none for user virtual addresses. Returns the new page directory, or a null pointer if memory allocation fails.
 */
uint64_t *
pml4_create (void) {
	uint64_t *pml4 = palloc_get_page (0);                                    // Allocate a new page for PML4 without zeroing
	if (pml4)
		memcpy (pml4, base_pml4, PGSIZE);                                    // Copy kernel mappings from base_pml4
	return pml4;                                                             // Return the new PML4
}

/* Applies the function func to each available page table entry within a page table.
 * Traverses using indices associated with the PT, applying provided aux data.
 */
static bool
pt_for_each (uint64_t *pt, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index, unsigned pdx_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the PT
		uint64_t *pte = &pt[i];                                              // Get the page table entry
		if (((uint64_t) *pte) & PTE_P) {                                     // If the entry is present
			void *va = (void *) (((uint64_t) pml4_index << PML4SHIFT) |      // Compute virtual address from indices
								 ((uint64_t) pdp_index << PDPESHIFT) |
								 ((uint64_t) pdx_index << PDXSHIFT) |
								 ((uint64_t) i << PTXSHIFT));
			if (!func (pte, va, aux))                                        // Apply the function
				return false;                                                // Return false if function application fails
		}
	}
	return true;                                                             // Return true if all entries processed successfully
}

/* Applies the function func to each available page table entry within a page directory.
 * Traverses using indices associated with the PD, applying provided auxiliary data.
 */
static bool
pgdir_for_each (uint64_t *pdp, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the PD
		uint64_t *pte = ptov((uint64_t *) pdp[i]);                           // Get the page directory entry
		if (((uint64_t) pte) & PTE_P)                                        // If the entry is present
			if (!pt_for_each ((uint64_t *) PTE_ADDR (pte), func, aux,        // Apply function to its corresponding page table
					pml4_index, pdp_index, i))
				return false;                                                // Return false if function application fails
	}
	return true;                                                             // Return true if all entries processed successfully
}

/* Applies the function func to each available page table entry within a page directory pointer table.
 * Traverses using indices associated with the PDP, applying provided auxiliary data.
 */
static bool
pdp_for_each (uint64_t *pdp,
		pte_for_each_func *func, void *aux, unsigned pml4_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the PDP
		uint64_t *pde = ptov((uint64_t *) pdp[i]);                           // Get the page directory pointer entry
		if (((uint64_t) pde) & PTE_P)                                        // If the entry is present
			if (!pgdir_for_each ((uint64_t *) PTE_ADDR (pde), func,          // Apply function to its corresponding page directory
					 aux, pml4_index, i))
				return false;                                                // Return false if function application fails
	}
	return true;                                                             // Return true if all entries processed successfully
}

/* Apply function func to each available page table entry, including kernel's. */
bool
pml4_for_each (uint64_t *pml4, pte_for_each_func *func, void *aux) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the PML4
		uint64_t *pdpe = ptov((uint64_t *) pml4[i]);                         // Get the page map level 4 entry
		if (((uint64_t) pdpe) & PTE_P)                                       // If the entry is present
			if (!pdp_for_each ((uint64_t *) PTE_ADDR (pdpe), func, aux, i))  // Apply function to its corresponding PDP
				return false;                                                // Return false if function application fails
	}
	return true;                                                             // Return true if all entries processed successfully
}

/* Destroys a page table by freeing all pages it references. */
static void
pt_destroy (uint64_t *pt) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the page table
		uint64_t *pte = ptov((uint64_t *) pt[i]);                            // Get the page table entry
		if (((uint64_t) pte) & PTE_P)                                        // If the entry is present
			palloc_free_page ((void *) PTE_ADDR (pte));                      // Free the allocated page frame
	}
	palloc_free_page ((void *) pt);                                          // Free the page table itself
}

/* Destroys a page directory by freeing all pages it references. */
static void
pgdir_destroy (uint64_t *pdp) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the page directory
		uint64_t *pte = ptov((uint64_t *) pdp[i]);                           // Get the page directory entry
		if (((uint64_t) pte) & PTE_P)                                        // If the entry is present
			pt_destroy (PTE_ADDR (pte));                                     // Destroy its corresponding page table
	}
	palloc_free_page ((void *) pdp);                                         // Free the page directory itself
}

/* Destroys a page directory pointer table by freeing all pages it references. */
static void
pdpe_destroy (uint64_t *pdpe) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {             // Iterate through all entries in the PDP
		uint64_t *pde = ptov((uint64_t *) pdpe[i]);                          // Get the page directory pointer entry
		if (((uint64_t) pde) & PTE_P)                                        // If the entry is present
			pgdir_destroy ((void *) PTE_ADDR (pde));                        // Destroy its corresponding page directory
	}
	palloc_free_page ((void *) pdpe);                                        // Free the page directory pointer itself
}

/* Destroys a PML4, freeing all the pages it references. */
void
pml4_destroy (uint64_t *pml4) {
	if (pml4 == NULL)                                                        // If the PML4 is NULL
		return;
	ASSERT (pml4 != base_pml4);                                              // Assert that it is not the base PML4

	/* if PML4 (vaddr) >= 1, it's kernel space by define. */
	uint64_t *pdpe = ptov ((uint64_t *) pml4[0]);                            // Get the kernel space directory pointer entry
	if (((uint64_t) pdpe) & PTE_P)
		pdpe_destroy ((void *) PTE_ADDR (pdpe));                            // Destroy its corresponding directory pointer
	palloc_free_page ((void *) pml4);                                        // Free the PML4 itself
}

/* Loads the page directory PD into the CPU's page directory base register. */
void
pml4_activate (uint64_t *pml4) {
	lcr3 (vtop (pml4 ? pml4 : base_pml4));                                   // Load the page directory base register with the physical address
}

/* Looks up the physical address that corresponds to user virtual address UADDR in pml4.
 * Returns the kernel virtual address corresponding to that physical address, or a null pointer if UADDR is unmapped.
 */
void *
pml4_get_page (uint64_t *pml4, const void *uaddr) {
	ASSERT (is_user_vaddr (uaddr));                                          // Assert that the address is a user virtual address

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) uaddr, 0);                  // Get the page table entry for the virtual address

	if (pte && (*pte & PTE_P))                                               // If the page table entry is present
		return ptov (PTE_ADDR (*pte)) + pg_ofs (uaddr);                      // Return the kernel virtual address corresponding to the physical address
	return NULL;                                                             // Return NULL if the page is not mapped
}

/* Adds a mapping in PML4 from user virtual page UPAGE to the physical frame identified by kernel virtual address KPAGE.
 * UPAGE must not already be mapped. KPAGE should be a page obtained from the user pool.
 * If WRITABLE is true, the new page is read/write; otherwise, it is read-only.
 * Returns true if successful, false if memory allocation failed.
 */
bool
pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw) {
	ASSERT (pg_ofs (upage) == 0);                                            // Assert that upage is page-aligned
	ASSERT (pg_ofs (kpage) == 0);                                            // Assert that kpage is page-aligned
	ASSERT (is_user_vaddr (upage));                                          // Assert that upage is a user virtual address
	ASSERT (pml4 != base_pml4);                                              // Assert that pml4 is not the base PML4

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) upage, 1);                  // Get or create the page table entry for the virtual address

	if (pte)
		*pte = vtop (kpage) | PTE_P | (rw ? PTE_W : 0) | PTE_U;              // Set the page table entry with frame address and flags
	return pte != NULL;                                                      // Return true if the mapping was successful
}

/* Marks user virtual page UPAGE "not present" in page directory PD.
 * Later accesses to the page will fault. Other bits in the page table entry are preserved.
 */
void
pml4_clear_page (uint64_t *pml4, void *upage) {
	uint64_t *pte;
	ASSERT (pg_ofs (upage) == 0);                                            // Assert that upage is page-aligned
	ASSERT (is_user_vaddr (upage));                                          // Assert that upage is a user virtual address

	pte = pml4e_walk (pml4, (uint64_t) upage, false);                        // Get the page table entry for the virtual address

	if (pte != NULL && (*pte & PTE_P) != 0) {                                // If the entry is present
		*pte &= ~PTE_P;                                                      // Clear the present bit
		if (rcr3 () == vtop (pml4))                                          // If the current page directory is active
			invlpg ((uint64_t) upage);                                       // Invalidate the page in TLB
	}
}

/* Returns true if the PTE for virtual page VPAGE in PML4 is dirty,
 * that is, if the page has been modified since the PTE was installed.
 * Returns false if PML4 contains no PTE for VPAGE.
 */
bool
pml4_is_dirty (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);              // Get the page table entry for the virtual address
	return pte != NULL && (*pte & PTE_D) != 0;                               // Return true if the dirty bit is set
}

/* Set the dirty bit to DIRTY in the PTE for virtual page VPAGE in PML4. */
void
pml4_set_dirty (uint64_t *pml4, const void *vpage, bool dirty) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);              // Get the page table entry for the virtual address
	if (pte) {                                                               // If the entry exists
		if (dirty)
			*pte |= PTE_D;                                                   // Set the dirty bit if dirty is true
		else
			*pte &= ~(uint32_t) PTE_D;                                       // Clear the dirty bit if dirty is false

		if (rcr3 () == vtop (pml4))                                          // If the current page directory is active
			invlpg ((uint64_t) vpage);                                       // Invalidate the page in TLB
	}
}

/* Returns true if the PTE for virtual page VPAGE in PML4 has been
 * accessed recently, that is, between the time the PTE was installed and the last time it was cleared.
 * Returns false if PML4 contains no PTE for VPAGE.
 */
bool
pml4_is_accessed (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);              // Get the page table entry for the virtual address
	return pte != NULL && (*pte & PTE_A) != 0;                               // Return true if the accessed bit is set
}

/* Sets the accessed bit to ACCESSED in the PTE for virtual page VPAGE in PML4. */
void
pml4_set_accessed (uint64_t *pml4, const void *vpage, bool accessed) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);              // Get the page table entry for the virtual address
	if (pte) {                                                               // If the entry exists
		if (accessed)
			*pte |= PTE_A;                                                   // Set the accessed bit if accessed is true
		else
			*pte &= ~(uint32_t) PTE_A;                                       // Clear the accessed bit if accessed is false

		if (rcr3 () == vtop (pml4))                                          // If the current page directory is active
			invlpg ((uint64_t) vpage);                                       // Invalidate the page in TLB
	}
}



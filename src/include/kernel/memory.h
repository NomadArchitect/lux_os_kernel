/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <kernel/boot.h>

/* this must be defined on a platform-specific basis */
/* it defines the page size and other necessary attributes for paging */
#include <platform/mmap.h>

#define PMM_CONTIGUOUS_LOW      0x01

// these flags control allocated memory
#define VMM_USER                0x01        // kernel-user toggle
#define VMM_EXEC                0x02
#define VMM_WRITE               0x04

// these flags are used as platform-independent status codes after page faults
#define VMM_PAGE_FAULT_PRESENT  0x01        // caused by a present page
#define VMM_PAGE_FAULT_USER     0x02        // caused by a user process
#define VMM_PAGE_FAULT_WRITE    0x04        // caused by a write operation
#define VMM_PAGE_FAULT_FETCH    0x08        // caused by instruction fetch

typedef struct {
    uint64_t highestPhysicalAddress;
    uint64_t lowestUsableAddress;
    uint64_t highestUsableAddress;
    size_t highestPage;
    size_t usablePages, usedPages;
    size_t reservedPages;
} PhysicalMemoryStatus;

void pmmInit(KernelBootInfo *);
void pmmStatus(PhysicalMemoryStatus *);
uintptr_t pmmAllocate(void);
uintptr_t pmmAllocateContiguous(size_t, int);
int pmmFree(uintptr_t);
int pmmFreeContiguous(uintptr_t, size_t);

void vmmInit();
uintptr_t vmmAllocate(size_t, int);
int vmmFree(uintptr_t, size_t);
int vmmPageFault(uintptr_t, int);       // the platform-specific page fault handler must call this

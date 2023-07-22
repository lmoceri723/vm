#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "conversions.h"
#include "system.h"

#pragma comment(lib, "advapi32.lib")

BOOLEAN full_virtual_memory_test (VOID) {
    ULONG i;
    PULONG_PTR arbitrary_va;
    ULONG random_number;
    BOOL page_faulted;
    BOOL fault_handled;

    PULONG_PTR p;
    ULONG_PTR num_bytes;
    ULONG_PTR virtual_address_size_in_pages;

    p = (PULONG_PTR) allocate_memory(&num_bytes);

    virtual_address_size_in_pages = num_bytes / PAGE_SIZE;

    for (i = 0; i < MB(1) / 10; i++)
    {
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).

        // randex
        random_number = rand();

        random_number %= virtual_address_size_in_pages;

        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.

        page_faulted = FALSE;

        arbitrary_va = p + (random_number * PAGE_SIZE) / sizeof(ULONG_PTR);

        __try
        {
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            page_faulted = TRUE;
        }


        if (page_faulted) {
            fault_handled = page_fault_handler(arbitrary_va);
            if (fault_handled == FALSE)
            {
                return FALSE;
            }

            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        }
    }

    return TRUE;
}
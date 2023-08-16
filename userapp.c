#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "userapp.h"

#pragma comment(lib, "advapi32.lib")

#define NUM_ADDRESSES MB(1) / 10

ULONG_PTR fake_faults;
ULONG_PTR num_faults;
ULONG_PTR num_first_accesses;
ULONG_PTR num_reaccesses;


BOOLEAN full_virtual_memory_test (VOID) {

    PULONG_PTR arbitrary_va;
    ULONG random_number;
    BOOL page_faulted;
    PULONG_PTR p;
    ULONG_PTR num_bytes;
    ULONG_PTR num_addresses;
    ULONG_PTR local;

    ULONG_PTR virtual_address_size_in_pages;

    ULONG start_time;
    ULONG end_time;
    ULONG time_elapsed;

    p = (PULONG_PTR) allocate_memory(&num_bytes);

    virtual_address_size_in_pages = num_bytes / PAGE_SIZE;

    start_time = GetTickCount();
    num_addresses = NUM_ADDRESSES;
    for (unsigned i = 0; i < num_addresses; i++)
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

        random_number = rand();

        random_number %= virtual_address_size_in_pages;

        // Write the virtual address into each page
        page_faulted = FALSE;

        arbitrary_va = p + (random_number * PAGE_SIZE) / sizeof(ULONG_PTR);

        do {
            __try
            {
                local = *arbitrary_va;
                if (local != 0)
                {
                    if (local != (ULONG_PTR) arbitrary_va)
                    {
                        DebugBreak();
                    }
                    num_reaccesses++;
                }
                else
                {
                    num_first_accesses++;
                }

                *arbitrary_va = (ULONG_PTR) arbitrary_va;
                page_faulted = FALSE;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                page_faulted = TRUE;
            }

            page_fault_handler(arbitrary_va);
        } while (page_faulted == TRUE);
    }

    end_time = GetTickCount();
    time_elapsed = end_time - start_time;
    printf("full_virtual_memory_test : finished accessing %Iu random virtual addresses in %u ms (%f s)\n",
           num_addresses, time_elapsed, time_elapsed / 1000.0);
    printf("full_virtual_memory_test : took %Iu faults and %Iu fake faults\n", num_faults, fake_faults);


    return TRUE;
}
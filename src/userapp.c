#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "../include/userapp.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
#pragma comment(lib, "advapi32.lib")

// This corresponds to how many times a random va will be written to in our test
#define NUM_ADDRESSES MB(1) / 10


ULONG64 fake_faults;
ULONG64 num_faults;

ULONG64 num_first_accesses;
ULONG64 num_reaccesses;


VOID full_virtual_memory_test (VOID) {

    PULONG_PTR arbitrary_va;
    ULONG random_number;
    BOOL page_faulted;
    PULONG_PTR p;
    ULONG_PTR num_bytes;
    ULONG_PTR local;

    ULONG_PTR virtual_address_size_in_pages;

    ULONG start_time;
    ULONG end_time;
    ULONG time_elapsed;

    // This replaces a malloc call in our system
    // Right now, we are just giving a set amount to the caller
    p = (PULONG_PTR) allocate_memory(&num_bytes);

    virtual_address_size_in_pages = num_bytes / PAGE_SIZE;

    // This is where the test is actually ran
    start_time = GetTickCount();
    for (ULONG64 i = 0; i < NUM_ADDRESSES; i++)
    {
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address, and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).

        // This computes a random virtual address within our range

        // LM FUTURE FIX this is not a uniform distribution, also be wary of the max value of rand()
        random_number = rand(); // NOLINT(*-msc50-cpp)
        random_number %= virtual_address_size_in_pages;
        arbitrary_va = p + (random_number * PAGE_SIZE) / sizeof(ULONG_PTR);

        // Write the virtual address into each page
        page_faulted = FALSE;
        // Try to access the virtual address, continue entering the handler until the page fault is resolved
        do {
            __try
            {
                // We are trying to write the va as a number into the page contents associated with that va
                local = *arbitrary_va;
                // This breaks into the debugger if the local value is not the same as the va
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
        } while (TRUE == page_faulted);
    }

    end_time = GetTickCount();
    time_elapsed = end_time - start_time;
    printf("full_virtual_memory_test : finished accessing %d random virtual addresses in %lu ms (%f s)\n",
           NUM_ADDRESSES, time_elapsed, time_elapsed / 1000.0);
    printf("full_virtual_memory_test : took %llu faults and %llu fake faults\n", num_faults, fake_faults);
}

#pragma clang diagnostic pop
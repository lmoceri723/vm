#include <stdio.h>
#include <Windows.h>
#include "../include/userapp.h"
#include "../include/debug.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
#pragma comment(lib, "advapi32.lib")


// TODO ISSUES
// Aging and Trimming - where is he walking from and how far should he walk
// is he keeping a good differentiated set of ages
/*
 [10:42 PM] Landy Wang
The meeting was about aging and trimming pages in a virtual memory subsystem.
 The participants discussed the problem areas of high cycles consumption by the program,
 which were mainly caused by aging for 45% and modified writing for 45%.
 They also looked at an excerpt of a trace to analyze the issue.
 They agreed to look at the spots in the page file array and the single lock for tomorrow.

 */


// This corresponds to how many times a random va will be written to in our test
// We are using MB(x) as a placeholder for 2^20, the purpose of this is just to get a large number
// This number represents the number of times we will access a random virtual address in our range
// MB has no reflection on the actual size of the memory we are using
#define VA_SIZE_IN_PAGES  ((ULONG64) (NUMBER_OF_PHYSICAL_PAGES + NUMBER_OF_DISC_PAGES))
#define NUM_PASSTHROUGHS  ((ULONG64) 2)

ULONG64 num_trims = 0;

VOID full_virtual_memory_test(VOID) {

    PULONG_PTR arbitrary_va;
    // ULONG random_number;
    BOOL page_faulted;
    PULONG_PTR pointer;
    ULONG_PTR num_bytes;
    ULONG_PTR local;

    ULONG_PTR virtual_address_size_in_pages;

    ULONG start_time;
    ULONG end_time;
    ULONG time_elapsed;

    ULONG thread_id;
    ULONG thread_index;

    // Compute faulting stats for the thread using thread index in the array
    thread_id = GetCurrentThreadId();
    thread_index = 0;
    for (ULONG i = 0; i < NUMBER_OF_FAULTING_THREADS; i++)
    {
        if (faulting_thread_ids[i] == thread_id)
        {
            thread_index = i;
            break;
        }
    }

    PFAULT_STATS stats = &fault_stats[thread_index];

    printf("userapp.c : thread %lu started\n", thread_index);

    // This replaces a malloc call in our system
    // Right now, we are just giving a set amount to the caller
    pointer = (PULONG_PTR) allocate_memory(&num_bytes);

    virtual_address_size_in_pages = num_bytes / PAGE_SIZE;

    PULONG_PTR p_end = pointer + (virtual_address_size_in_pages * PAGE_SIZE) / sizeof(ULONG_PTR);

    ULONG64 slice_size = virtual_address_size_in_pages / NUMBER_OF_FAULTING_THREADS;
    ULONG64 slice_start = slice_size * thread_index;

    // This is where the test is actually ran
    arbitrary_va = pointer + slice_start;
    start_time = GetTickCount();

    for (ULONG64 passthrough = 0; passthrough < NUM_PASSTHROUGHS; passthrough++) {

        for (ULONG64 rep = 0; rep < VA_SIZE_IN_PAGES; ++rep) {
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

            // Take the last 3 bits in get tick count
            //  If the low 3 bits are less than 4, get the next address instead of getting a random address

            //        random_number = (GetTickCount() + i * (GetCurrentThreadId())) << 12;
            //        random_number %= virtual_address_size_in_pages;
            //
            //        arbitrary_va = p + (random_number * PAGE_SIZE) / sizeof(ULONG_PTR);
            //            i++;
            //
            //            if (i >= virtual_address_size_in_pages)
            //            {
            //                i = slice_start % virtual_address_size_in_pages;
            //            }
            //
            //            arbitrary_va = p + slice_start + (i * PAGE_SIZE) / sizeof(ULONG_PTR);

            if (rep % 10000 == 0) {
                printf(".");
            }

            // Calculate arbitrary VA from rep
            arbitrary_va = pointer + (rep * PAGE_SIZE) / sizeof(ULONG_PTR);

            // Write the virtual address into each page
            page_faulted = FALSE;
            // Try to access the virtual address, continue entering the handler until the page fault is resolved
            do {
                __try
                {
                    // Here we read the value of the page contents associated with the VA
                    local = *arbitrary_va;
                    // This causes an error if the local value is not the same as the VA
                    // This means that we mixed up page contents between different VAs
                    if (local != 0) {
                        if (local != (ULONG_PTR) arbitrary_va) {
                            fatal_error("full_virtual_memory_test : page contents are not the same as the VA");
                        }
                        stats->num_reaccesses++;
                    } else {
                        stats->num_first_accesses++;
                    }

                    // We are trying to write the VA as a number into the page contents associated with that VA
                    if ((PULONG_PTR) local != arbitrary_va) {
                        *arbitrary_va = (ULONG_PTR) arbitrary_va;
                    }

                    page_faulted = FALSE;
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    page_faulted = TRUE;
                }

                // We call the page fault handler no matter what
                // This is because we want to reset the age of a PTE when its VA is accessed
                // This is done in the handler, as is referred to in this program as a fake fault
                page_fault_handler(arbitrary_va, stats);

            } while (TRUE == page_faulted);
        }
    }

    // This gets the time elapsed in milliseconds
    end_time = GetTickCount();
    time_elapsed = end_time - start_time;

    // Consolidate into one print statement
    printf("\nfull_virtual_memory_test : thread %lu finished accessing %llu passthroughs "
           "of the virtual address space (%llu addresses total) in %lu ms (%f s)\n"
           "full_virtual_memory_test : thread %lu took %llu faults and %llu fake faults\n"
           "full_virtual_memory_test : thread %lu took %llu first accesses and %llu reaccesses\n\n",
           thread_index, NUM_PASSTHROUGHS, NUM_PASSTHROUGHS * VA_SIZE_IN_PAGES, time_elapsed, time_elapsed / 1000.0,
           thread_index, stats->num_faults, stats->num_fake_faults,
           thread_index, stats->num_first_accesses, stats->num_reaccesses);

    printf("full_virtual_memory_test : thread %lu took %llu trims\n", thread_index, num_trims);
}

// This function controls a faulting thread
DWORD faulting_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    // This thread needs to be able to react to handles for waking as well as exiting
    HANDLE handles[4];

    handles[0] = system_start_event;
    handles[1] = system_exit_event;

    WaitForSingleObject(system_start_event, INFINITE);

    full_virtual_memory_test();

    return 0;
}
#pragma clang diagnostic pop
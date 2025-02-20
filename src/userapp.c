#include <stdio.h>
#include <Windows.h>
#include <stdlib.h>
#include "../include/userapp.h"

#include <system.h>

#include "../include/debug.h"

#pragma comment(lib, "advapi32.lib")

// This corresponds to how many times a random va will be written to in our test
// We are using MB(x) as a placeholder for 2^20, the purpose of this is just to get a large number
// This number represents the number of times we will access a random virtual address in our range
// MB has no reflection on the actual size of the memory we are using
#define BAR_WIDTH 100
#define NUM_PASSTHROUGHS  ((ULONG64) 2)

ULONG64 num_trims = 0;
BOOL console_initialized = FALSE;

static VOID position_cursor(SHORT row, SHORT col) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {col, row};
    SetConsoleCursorPosition(hConsole, pos);
    fflush(stdout);
}

static VOID print_bar(int thread_id, double fraction, ULONG64 passthrough, ULONG64 total_passthroughs, ULONG64 faults) {
    int fill = (int)(fraction * BAR_WIDTH);
    char bar[BAR_WIDTH + 1];
    for (int i = 0; i < BAR_WIDTH; i++) {
        bar[i] = (i < fill) ? '#' : ' ';
    }
    bar[BAR_WIDTH] = '\0';

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {0, (SHORT)thread_id};
    SetConsoleCursorPosition(hConsole, pos);

    double percent = fraction * 100.0;

    printf("\x1b[33mThread %d\x1b[0m [", thread_id);
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (bar[i] == '#') {
            printf("\x1b[32m#\x1b[0m");
        } else {
            printf(" ");
        }
    }
    printf("] Pass %llu/%llu Faults: %llu (%.2f%%)", passthrough, total_passthroughs, faults, percent);
    fflush(stdout);
}

VOID setup_terminal(VOID) {
    if (!console_initialized) {
        InitializeCriticalSection(&console_lock);

        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

        system("cls");

        DWORD mode;
        GetConsoleMode(console, &mode);
        SetConsoleMode(console,
            mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                | DISABLE_NEWLINE_AUTO_RETURN
                | ENABLE_PROCESSED_OUTPUT);

        for (int i = 0; i < NUMBER_OF_FAULTING_THREADS; i++) {
            printf("\n");
        }

        console_initialized = TRUE;
    }
}

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

    setup_terminal();

    // Compute faulting stats for the thread using thread index in the array
    thread_id = GetCurrentThreadId();
    thread_index = 0;
    for (ULONG i = 0; i < NUMBER_OF_FAULTING_THREADS; i++) {
        if (faulting_thread_ids[i] == thread_id) {
            thread_index = i;
            break;
        }
    }

    SHORT thread_line = (SHORT)(thread_index);
    PFAULT_STATS stats = &fault_stats[thread_index];

    // Initialize thread's line
    EnterCriticalSection(&console_lock);
    position_cursor(thread_line, 0);
    printf("\rThread %lu: Starting test...", thread_index);
    fflush(stdout);
    LeaveCriticalSection(&console_lock);

    // This replaces a malloc call in our system
    // Right now, we are just giving a set amount to the caller
    pointer = (PULONG_PTR) allocate_memory(&num_bytes);

    virtual_address_size_in_pages = num_bytes / PAGE_SIZE;

    //PULONG_PTR p_end = pointer + (virtual_address_size_in_pages * PAGE_SIZE) / sizeof(ULONG_PTR);

    ULONG64 slice_size = virtual_address_size_in_pages / NUMBER_OF_FAULTING_THREADS;
    slice_size = slice_size * PAGE_SIZE / sizeof(ULONG_PTR);

    ULONG64 slice_start = slice_size * thread_index;

    // This is where the test is actually ran
    start_time = GetTickCount();

    for (ULONG64 passthrough = 0; passthrough < NUM_PASSTHROUGHS; passthrough++) {

        // printf("full_virtual_memory_test : thread %lu accessing passthrough %llu\n "
        //        "Total amount of accesses: %llu\n", thread_index, passthrough,
        //        virtual_address_size_in_pages * (passthrough + 1));

        for (ULONG64 rep = 0; rep < virtual_address_size_in_pages; ++rep) {
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

            // Calculate arbitrary VA from rep
            ULONG64 offset = slice_start + (rep * PAGE_SIZE) / sizeof(ULONG_PTR);
            offset = offset % num_bytes;
            arbitrary_va = pointer + offset;


            if (rep % 10000 == 0) {
                double fraction = (double)(rep + 1) / (double)virtual_address_size_in_pages;
                EnterCriticalSection(&console_lock);
                print_bar(thread_index, fraction, passthrough + 1, NUM_PASSTHROUGHS, stats->num_faults);
                LeaveCriticalSection(&console_lock);
            }

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

            } while (page_faulted == TRUE);
        }
    }

    // This gets the time elapsed in milliseconds
    end_time = GetTickCount();
    time_elapsed = end_time - start_time;

    // Final status update
    EnterCriticalSection(&console_lock);
    position_cursor(thread_line, 0);
    printf("\r\x1b[33mThread %lu\x1b[0m: Complete - %llu passes, %lu ms, Faults: %llu, Reaccesses: %llu    ",
        thread_index,
        NUM_PASSTHROUGHS,
        time_elapsed,
        stats->num_faults,
        stats->num_reaccesses
    );
    fflush(stdout);
    LeaveCriticalSection(&console_lock);

    // Consolidated into one print statement
    printf("\nfull_virtual_memory_test : thread %lu finished accessing %llu passthroughs "
           "of the virtual address space (%llu addresses total) in %lu ms (%f s)\n"
           "full_virtual_memory_test : thread %lu took %llu faults and %llu fake faults\n"
           "full_virtual_memory_test : thread %lu took %llu first accesses and %llu reaccesses\n\n",
           thread_index, NUM_PASSTHROUGHS, NUM_PASSTHROUGHS * virtual_address_size_in_pages, time_elapsed, time_elapsed / 1000.0,
           thread_index, stats->num_faults, stats->num_fake_faults,
           thread_index, stats->num_first_accesses, stats->num_reaccesses);
}

// This function controls a faulting thread
DWORD faulting_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    WaitForSingleObject(system_start_event, INFINITE);

    full_virtual_memory_test();

    return 0;
}
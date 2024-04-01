#include <Windows.h>
#include <stdio.h>
#include "../include/structs.h"
#include "../include/system.h"
#include "../include/debug.h"
#include "../include/userapp.h"

// This function puts an individual page on the modified list given its PTE
void trim(PPTE pte)
{
    num_trims++;

    PPFN pfn;
    PFN pfn_contents;
    PTE old_pte_contents;
    PTE new_pte_contents;
    PVOID user_va;

    pfn = pfn_from_frame_number(pte->memory_format.frame_number);

    lock_pfn(pfn);

    user_va = va_from_pte(pte);
    NULL_CHECK(user_va, "trim : could not get the va connected to the pte")

    // The user VA is still mapped, we need to unmap it here to stop the user from changing it
    // Any attempt to modify this va will lead to a page fault so that we will not be able to have stale data
    unmap_pages(user_va, 1);

    // This writes the new contents into the PTE and PFN
    old_pte_contents = read_pte(pte);
    assert(old_pte_contents.memory_format.valid == 1)

    // The PTE is zeroed out here to ensure no stale data remains
    new_pte_contents.entire_format = 0;
    new_pte_contents.transition_format.frame_number = old_pte_contents.memory_format.frame_number;
    write_pte(pte, new_pte_contents);

    pfn_contents = read_pfn(pfn);
    pfn_contents.flags.state = MODIFIED;
    write_pfn(pfn, pfn_contents);

    // Add the page to the modified list
    EnterCriticalSection(&modified_page_list.lock);

    add_to_list(pfn, &modified_page_list);

    LeaveCriticalSection(&modified_page_list.lock);

    unlock_pfn(pfn);

    SetEvent(modified_writing_event);
}

// This function ages PTEs by incrementing their age by 1
// If the age is 7, then the page is trimmed
// Their age is reset when they are accessed by a program
// This is a very simple aging algorithm that is not very effective
// It is only used as a placeholder until a better algorithm is implemented
VOID age_pages()
{
    PPTE pte;
    pte = pte_base;

    // Iterates over all PTEs and ages them
    while (pte != pte_end)
    {
        lock_pte(pte);
        if (pte->memory_format.valid == 1)
        {
            if (pte->memory_format.age == 7)
            {
                trim(pte);
            }
            else
            {
                pte->memory_format.age += 1;
            }
        }
        unlock_pte(pte);
        pte++;
    }
}

// No functions get to call this, it must be invoked in its own thread context
DWORD trim_thread(PVOID context) {
    // This parameter only exists to satisfy the API requirements for a thread starting function
    UNREFERENCED_PARAMETER(context);

    // We wait on two handles here in order to react by terminating when the system exits
    // Or react by waking up and trimming pages if necessary
    HANDLE handles[2];

    handles[0] = system_exit_event;
    handles[1] = wake_aging_event;

    // Wait for the system to start before beginning to trim pages
    WaitForSingleObject(system_start_event, INFINITE);
    printf("trimmer.c : aging and trimming thread started\n");

    // This thread infinitely waits to be woken up by other threads until the end of the program
    while (TRUE)
    {
        // Exits the infinite loop if told to, otherwise evaluates whether trimming is needed
        ULONG index = WaitForMultipleObjects(ARRAYSIZE(handles), handles,
                                             FALSE, INFINITE);
        if (index == 0)
        {
            break;
        }

        // This is an arbitrary condition that prompts us to trim pages when we need to
        // In the future we will come up with a new algorithmic approach to doing this
        while (free_page_list.num_pages + standby_page_list.num_pages <= physical_page_count / 4
               && free_page_list.num_pages + standby_page_list.num_pages + modified_page_list.num_pages != physical_page_count)
        {
            age_pages();
        }
    }
    // This return statement only exists to satisfy the API requirements for a thread starting function
    return 0;
}

DWORD aging_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    HANDLE handles[2];
    handles[0] = system_exit_event;
    handles[1] = wake_aging_event;

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE)
    {
        ULONG index = WaitForMultipleObjects(ARRAYSIZE(handles), handles,
                                             FALSE, INFINITE);
        if (index == 0)
        {
            break;
        }

        /* Plan for aging pages */
        // 1. When woken, find the rate pages are leaving the free or standby lists
        // 2. Estimate a duration until the lists will be empty
        // 4. Divide the duration by 8 to get 8 increments, one for each age
        // 5. For each respective increment, divide the 100% of the va space
        // between the amount of seconds until the next increment
        // That way, every second a small amount of work will be done
        // Instead of a large amount of work being done all at once
        // 6. Do the required passthrough
        // 7. Sleep until it is time for the next passthrough

    }

    return 0;
}

HANDLE trim_wake_event;
DWORD trimming_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    HANDLE handles[2];
    handles[0] = system_exit_event;
    handles[1] = trim_wake_event;

    WaitForSingleObject(system_start_event, INFINITE);

    while (TRUE)
    {
        ULONG index = WaitForMultipleObjects(ARRAYSIZE(handles), handles,
                                             FALSE, INFINITE);
        if (index == 0)
        {
            break;
        }

        /* Plan for trimming pages */
        // Batch unmap_pages() calls done in age_pages()
        // The aging thread now keeps track of the pages it wants to trim instead of directly trimming them
        // When the amount of pages to trim is greater than a threshold (64, 256, i. e)
        // Or before the thread goes to sleep
        // The aging thread wakes up the trimming thread and tells it to trim the specified pages
        // The trimming thread then trims the pages, batching the unmap_pages() calls
        // This fixes the problem of contention during unmap calls and speeds up aging dramatically

    }
    return 0;
}

#include <Windows.h>
#include <stdio.h>
#include "include/structs.h"
#include "include/system.h"
#include "include/debug.h"



// TODO read over this and write comments
// TODO write this to accept a pte instead of a pfn
// This is better to be done after finalizing aging/trimming pages
// This function puts an individual page on the modified list
void trim(PPFN page)
{
    PVOID user_va = va_from_pte(page->pte);
    if (user_va == NULL)
    {
        printf("trim : could not get the va connected to the pte");
        fatal_error();
    }

    // The user va is still mapped, we need to unmap it here to stop the user from changing it
    // Any attempt to modify this va will lead to a page fault so that we will not be able to have stale data
    if (MapUserPhysicalPages (user_va, 1, NULL) == FALSE) {

        printf ("trim : could not unmap VA %p to page %llX\n", user_va, frame_number_from_pfn(page));
        fatal_error();
    }

    // This is safe as I hold both locks already here
    PPTE pte = page->pte;
    PTE old_pte_contents = read_pte(pte);
    PTE new_pte_contents;
    new_pte_contents.entire_format = 0;

    assert(old_pte_contents.memory_format.valid == 1)

    assert(new_pte_contents.transition_format.always_zero == 0)
    new_pte_contents.transition_format.frame_number = old_pte_contents.memory_format.frame_number;

    write_pte(pte, new_pte_contents);

    page->flags.state = MODIFIED;
    EnterCriticalSection(&modified_page_list.lock);

    add_to_list(page, &modified_page_list, TRUE);

    LeaveCriticalSection(&modified_page_list.lock);

    SetEvent(modified_writing_event);
}

// TODO refine this by implementing the strategies below
//  Afterwards, read over this and write comments
// Age based on consumption
// Hop over pte regions, which will have both a bit-map and valid count / fancy skipping
// 33 bytes
// combine 0 and 256 by doing another read (try to see if 255 and 256 can be combined instead
VOID age_pages()
{
    PPTE pte;
    PPFN pfn;

    pte = pte_base;

    while (pte != pte_end)
    {
        lock_pte(pte);
        if (pte->memory_format.valid == 1)
        {
            if (pte->memory_format.age == 7)
            {
                pfn = pfn_from_frame_number(pte->memory_format.frame_number);

                lock_pfn(pfn);
                trim(pfn);
                unlock_pfn(pfn);
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

    // We wait on two handles here in order to react by terminating when the system exits
    // Or react by waking up and trimming pages if necessary
    HANDLE handles[2];

    handles[0] = system_exit_event;
    handles[1] = wake_aging_event;

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

        // TODO LM FIX we want to rewrite this to try and meet a target given to this thread based on previous demand
        while (free_page_list.num_pages + standby_page_list.num_pages <= physical_page_count / 4
               && free_page_list.num_pages + standby_page_list.num_pages + modified_page_list.num_pages != physical_page_count)
        {
            age_pages();
        }
    }
    // This return statement only exists to satisfy the API requirements for a thread starting function
    return 0;
}

#include <Windows.h>
#include "../include/vm.h"
#include "../include/debug.h"

#define MAX_MOD_BATCH                   256

#define PAGE_WRITE_FUNCTION            write_page_to_disc

BOOLEAN write_pages_to_disc(VOID)
{
    ULONG64 target_pages;
    PFN_LIST batch_list;
    PPFN pfn;
    PFN local;

    // Initialize the list of pages to write to disc
    initialize_listhead(&batch_list);
    batch_list.num_pages = 0;

    // Lock the list of modified pages
    EnterCriticalSection(&modified_page_list.lock);

    // Check the count of the list. If the list is empty, we can return
    if (modified_page_list.num_pages == 0 || free_disc_spot_count == 0)
    {
        LeaveCriticalSection(&modified_page_list.lock);
        return FALSE;
    }

    target_pages = MAX_MOD_BATCH;

    // Pop the modified pages
    batch_pop_from_list_head(&modified_page_list, &batch_list, target_pages);
    LeaveCriticalSection(&modified_page_list.lock);

    target_pages = batch_list.num_pages;

    // Get disc indices for each page
    ULONG64 disc_indices[target_pages];
    ULONG num_returned_indices = get_disc_indices(disc_indices, target_pages);
    // Add any pages back to the modified list that we couldn't get disc indices for
    if (num_returned_indices < target_pages) {
        EnterCriticalSection(&modified_page_list.lock);
        while (!is_list_empty(&batch_list))
        {
            pfn = pop_from_list_head(&batch_list);
            add_to_list_head(pfn, &modified_page_list);
        }
        LeaveCriticalSection(&modified_page_list.lock);

        // We need to unlock the PFNs we have locked
        PLIST_ENTRY entry = batch_list.entry.Flink;
        for (ULONG64 i = 0; i < num_returned_indices; i++)
        {
            pfn = CONTAINING_RECORD(entry, PFN, entry);
            unlock_pfn(pfn);
            entry = entry->Flink;
        }
    }
    if (num_returned_indices == 0)
    {
        return FALSE;
    }

    ULONG64 frame_numbers[target_pages];

    // Update the reference counts and relinquish locks
    PLIST_ENTRY entry = batch_list.entry.Flink;
    for (ULONG64 i = 0; i < target_pages; i++)
    {
        pfn = CONTAINING_RECORD(entry, PFN, entry);
        pfn->flags.reference += 1;
        unlock_pfn(pfn);
        frame_numbers[i] = frame_number_from_pfn(pfn);
        entry = entry->Flink;
    }

    // Map the pages to our private VA space
    // This might be useless
    EnterCriticalSection(&modified_write_va_lock);

    map_pages(modified_write_va, target_pages, frame_numbers);

    // For each page, copy the contents to the paging file according to its corresponding disc index
    for (ULONG64 i = 0; i < target_pages; i++)
    {
        PVOID actual_space;
        actual_space = (PVOID) ((ULONG_PTR) page_file + (disc_indices[i] * PAGE_SIZE));

        memcpy(actual_space, modified_write_va, PAGE_SIZE);

        // Update the PFN to reflect the new disc index
        pfn = pfn_from_frame_number(frame_numbers[i]);
        lock_pfn(pfn);
        local = read_pfn(pfn);

        // If the page has not been modified during the write, we update the PFN to hold a its disc index
        if (local.flags.modified == 0) {
            local.disc_index = disc_indices[i];
            local.flags.state = STANDBY;
            local.flags.reference -= 1;
            write_pfn(pfn, local);
        }
        // Otherwise, we free the disc index and reset anything to do with references in the PFN
        else {
            local.flags.reference -= 1;
            local.flags.modified = 0;
            write_pfn(pfn, local);

            free_disc_index(disc_indices[i]);
        }
    }

    unmap_pages(modified_write_va, target_pages);

    LeaveCriticalSection(&modified_write_va_lock);

    // Add the pages to the standby list
    EnterCriticalSection(&standby_page_list.lock);
    while (!is_list_empty(&batch_list)) {
        pfn = pop_from_list_head(&batch_list);
        add_to_list_tail(pfn, &standby_page_list);

        unlock_pfn(pfn);
    }

    LeaveCriticalSection(&standby_page_list.lock);

    // Signal to other threads that pages are available
    SetEvent(pages_available_event);

    return TRUE;
}

// This controls the thread that constantly writes pages to disc when prompted by other threads
DWORD modified_write_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    // This thread needs to be able to react to handles for waking as well as exiting
    HANDLE handles[3];

    handles[0] = system_exit_event;
    handles[1] = modified_writing_event;
    handles[2] = disc_spot_available_event;

    ULONG64 prev_num_free_pages = 0;
    ULONG64 prev_time = 0;

    ULONG64 num_free = 0;
    ULONG64 time = 0;


    // This waits for the system to start before doing anything
    WaitForSingleObject(system_start_event, INFINITE);
    printf("mod_writer.c : modified write thread started\n");

    while (TRUE)
    {
        // Waits for both events, doing different actions based on each outcome
        ULONG index = WaitForMultipleObjects(ARRAYSIZE(handles), handles,
                                             FALSE, INFINITE);
        // Exits the function, terminating the thread
        if (index == 0)
        {
            break;
        }

        while (TRUE) {
            // TODO implement this count
            num_free = number_of_free_pages;
            time = GetTickCount();

            // This is the amount of time passed since the last time this thread was active
            ULONG64 delta_time = time - prev_time;

            // This is the amount of pages that have been consumed in the last delta_time ms
            ULONG64 delta_free_pages = prev_num_free_pages - num_free;

            // Calculate the rate of page consumption per millisecond
            double consumption_rate = (double) delta_free_pages / (double) delta_time;

            // Calculate number of batches to write per delta_time interval
            ULONG64 num_batches = (ULONG64) (consumption_rate * delta_time);

            // Write a batch
            if (write_pages_to_disc() == FALSE)
            {
                // If we couldn't write any pages, we need to break out of the loop and wait for an event
                // Since we are out of mod pages or disc indices
                break;
            }
        }
    }

    // This function doesn't actually return anything as it runs infinitely throughout the duration of the program
    // This return statement only exists to satisfy the API requirements for a thread starting function
    return 0;
}

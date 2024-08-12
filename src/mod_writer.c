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
    //EnterCriticalSection(&disc_in_use_lock);

    // Check the count of the list. If the list is empty, we can return
    if (modified_page_list.num_pages == 0 || free_disc_spot_count == 0)
    {
        //LeaveCriticalSection(&disc_in_use_lock);
        LeaveCriticalSection(&modified_page_list.lock);
        return FALSE;
    }

    target_pages = MAX_MOD_BATCH;

    // We want to write as many pages as possible to disc, so we aim to write 256 or more pages
    if (target_pages > modified_page_list.num_pages)
    {
        target_pages = modified_page_list.num_pages;
    }
    if (target_pages > free_disc_spot_count)
    {
        target_pages = free_disc_spot_count;
    }

    // Get disc indexes for the pages
    ULONG64 disc_indices[MAX_MOD_BATCH];

    for (ULONG64 i = 0; i < target_pages; i++)
    {
        // TODO Figure out a locking solution here
        //ULONG64 disc_index = get_disc_index_with_lock();
        ULONG64 disc_index = get_disc_index();
        if (disc_index == MAX_ULONG64)
        {
            fatal_error("write_pages_to_disc : could not get a disc index when we should have been able to");
        }
        disc_indices[i] = disc_index;
    }
    //LeaveCriticalSection(&disc_in_use_lock);

    // This could return a frame number array
    batch_pop_from_list_head(&modified_page_list, &batch_list, target_pages);

    // Call free_disc_space on the last (target_pages - batch_list.num_pages) disc_indices
    if (batch_list.num_pages != target_pages)
    {
        //EnterCriticalSection(&disc_in_use_lock);

        for (ULONG64 i = batch_list.num_pages; i < target_pages; i++)
        {
            // TODO locking here too
            free_disc_index(disc_indices[i]);
        }

        //LeaveCriticalSection(&disc_in_use_lock);
    }

    LeaveCriticalSection(&modified_page_list.lock);

    target_pages = batch_list.num_pages;
    if (target_pages == 0)
    {
        return FALSE;
    }

    // Get the frame numbers of the pages
    ULONG64 frame_numbers[MAX_MOD_BATCH];
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
    // Thi might be useless
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
        if (local.flags.modified == 0)
        {
            local.disc_index = disc_indices[i];
            local.flags.state = STANDBY;
            local.flags.reference -= 1;
            write_pfn(pfn, local);
        }
        else
        {
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
    while (!is_list_empty(&batch_list))
    {
        pfn = pop_from_list_head(&batch_list);
        add_to_list_tail(pfn, &standby_page_list);

        // TODO LM ASK IS THIS CORRECT?
        unlock_pfn(pfn);
    }

    LeaveCriticalSection(&standby_page_list.lock);

    // Signal to other threads that pages are available
    SetEvent(pages_available_event);

    return TRUE;
}

// This function takes the first modified page and writes it to the paging file
BOOLEAN write_page_to_disc(VOID)
{
    PPFN pfn;
    PFN local;
    ULONG_PTR frame_number;
    ULONG64 disc_index;

    pfn = pop_from_list_head(&modified_page_list);
    if (pfn == NULL)
    {
        return FALSE;
    }

    frame_number = frame_number_from_pfn(pfn);

    EnterCriticalSection(&modified_write_va_lock);
    // We need to map the page into our own private VA space to copy its contents
    map_pages(modified_write_va, 1, &frame_number);

    disc_index = get_disc_index();

    // At this point, we cannot get a disc index, so we put the page back on the modified list and wait for a spot
    // This is a last resort option, as we have to completely undo everything and try again
    if (disc_index == MAX_ULONG64)
    {
        unmap_pages(modified_write_va, 1);

        EnterCriticalSection(&modified_page_list.lock);

        // We removed from the head of the list, so we want to add back at the head
        add_to_list_head(pfn, &modified_page_list);

        LeaveCriticalSection(&modified_write_va_lock);

        LeaveCriticalSection(&modified_page_list.lock);

        unlock_pfn(pfn);

        // This signifies to the calling function that we failed
        return FALSE;
    }

    // This computes the actual address of where we want to write the page contents in the paging file and copies it
    PVOID actual_space;
    actual_space = (PVOID) ((ULONG_PTR) page_file + (disc_index * PAGE_SIZE));

    memcpy(actual_space, modified_write_va, PAGE_SIZE);

    // We can now unmap this from our va space as we have finished copying its contents to disc
    unmap_pages(modified_write_va, 1);

    LeaveCriticalSection(&modified_write_va_lock);

    // Instead of storing this index in the PTE, we store it in the PFN.
    // We do this because a PTE is too small to hold both a disc index and a frame number.
    // This works because we always want to access a frame number over a disc index
    // This allows us to extend the size of both fields instead of trying to cram them together in a PTE

    local = read_pfn(pfn);
    local.disc_index = disc_index;
    local.flags.state = STANDBY;
    write_pfn(pfn, local);

    EnterCriticalSection(&standby_page_list.lock);

    add_to_list_tail(pfn, &standby_page_list);

    LeaveCriticalSection(&standby_page_list.lock);

    unlock_pfn(pfn);

    // Now that we have successfully put a page on the standby list,
    // We can signal to other threads that one is available
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

        // This is an arbitrary condition that prompts us to write to disc when we need to
        // In the future we will come up with a new algorithmic approach to doing this
        while (modified_page_list.num_pages >= physical_page_count / 4)
        {
            if (PAGE_WRITE_FUNCTION() == FALSE)
            {
                break;
            }
        }
    }

    // This function doesn't actually return anything as it runs infinitely throughout the duration of the program
    // This return statement only exists to satisfy the API requirements for a thread starting function
    return 0;
}

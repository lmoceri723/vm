#include "../include/debug.h"
#include "../include/system.h"

// TODO LM FIX make this work with a 64 bit ULONG64
// This is very important, as it speeds this iteration up exponentially
// This is because the CPU will now be able to check 64 spots instead of 8 (char size spots) in a single register
ULONG64 get_disc_index(VOID)
{
    PULONG64 disc_spot;
    ULONG64 spot_cluster;
    ULONG index_in_cluster;

    EnterCriticalSection(&disc_in_use_lock);

    // Reads each char of our bit map and breaks when it finds a non-full char
    disc_spot = disc_in_use;
    while (disc_spot != disc_in_use_end)
    {
        if (*disc_spot != FULL_BITMAP_CHUNK)
        {
            break;
        }
        disc_spot++;
    }

    // In this case, no disc spots are available
    // This returns to the modified_write_thread, which waits for a disc spot to be free
    if (disc_spot == disc_in_use_end)
    {
        LeaveCriticalSection(&disc_in_use_lock);
        return FULL_BITMAP_CHUNK;
    }

    // This reads in the char (byte) with an empty slot
    spot_cluster = *disc_spot;
    index_in_cluster = 0;
    while (TRUE)
    {
        // First, we check if the leftmost bit is a zero
        // We do this by comparing it to a one with LOGICAL AND
        if ((spot_cluster & 0x1) == FALSE)
        {
            // Once we find this bit, we set it inside disc_spot and break in order to return the correct index
            // We set this bit by using a LOGICAL OR (|=) to compare our char to a comparison value
            // With a set bit at the index we want to set
            // This sets our bit to one without changing other bits
            // We then left shift this value by index_in_cluster bits
            // In order to match the positions of our two bits
            *disc_spot |= (1 << index_in_cluster);
            break;
        }
        index_in_cluster++;
        // This throws away the rightmost lowest bit, shifts every bit to the right, and the highest bit is set to zero
        // This is safe to do as we know that one of the 8 bits we are checking will be a zero
        // So we will never get to this pocket of zeroes at the end
        // We use this to always have the bit we want to check as the first bit
        spot_cluster = spot_cluster>>1;
    }

    LeaveCriticalSection(&disc_in_use_lock);
    // This returns the exact index inside disc_in_use that corresponds to the now occupied disc spot
    return (disc_spot - disc_in_use) * BITS_PER_BYTE + index_in_cluster;
}

// This function takes the first modified page and writes it to the disc
BOOLEAN write_page_to_disc(VOID)
{
    PPFN pfn;
    ULONG_PTR frame_number;
    BOOLEAN took_modified_page = FALSE;
    PPFN first_page;

    while (took_modified_page == FALSE) {
        // Peek at the head of the standby list
        first_page = CONTAINING_RECORD(modified_page_list.entry.Flink, PFN, entry);
        // Lock the pfn at the head
        lock_pfn(first_page);
        // Lock the list
        EnterCriticalSection(&modified_page_list.lock);
        if (modified_page_list.num_pages == 0)
        {
            LeaveCriticalSection(&modified_page_list.lock);
            unlock_pfn(first_page);
            return TRUE;
        }

        // If the frame numbers do not match up relinquish both locks and try again
        if (CONTAINING_RECORD(modified_page_list.entry.Flink, PFN, entry) != first_page) {
            LeaveCriticalSection(&modified_page_list.lock);
            unlock_pfn(first_page);
            continue;
        }

        // If the number's do match up, pop the page from the list
        pfn = pop_from_list(&modified_page_list, TRUE);
        // Relinquish lock for list
        LeaveCriticalSection(&modified_page_list.lock);
        took_modified_page = TRUE;
    }

    frame_number = frame_number_from_pfn(pfn);

    // We need to map the page into our own va private space to copy its contents
    if (MapUserPhysicalPages (modified_write_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_write_va,
                frame_number_from_pfn(pfn));
        fatal_error();
    }

    ULONG64 disc_index = get_disc_index();

    // At this point, we cannot get a disc index, so we put the page back on the modified list and wait for a spot
    // This is a last resort option, as we have to completely undo everything and try again
    if (disc_index == FULL_BITMAP_CHUNK)
    {
        if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_write_va,
                    frame_number_from_pfn(pfn));
            fatal_error();
        }

        EnterCriticalSection(&modified_page_list.lock);

        // LM FIX TODO we need to add back to the head
        add_to_list(pfn, &modified_page_list, TRUE);

        LeaveCriticalSection(&modified_page_list.lock);

        unlock_pfn(pfn);

        // This signifies to the calling function that we failed
        return FALSE;
    }

    // This computes the actual address of where we want to write the page contents in the paging file and copies it
    PVOID actual_space;
    actual_space = (PVOID) ((ULONG_PTR) disc_space + (disc_index * PAGE_SIZE));
    //LM MULTITHREADING FIX this will be problematic, we need to have a lock on this va
    memcpy(actual_space, modified_write_va, PAGE_SIZE);

    // We can now unmap this from our va space as we have finished copying its contents to disc
    if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_write_va,
                frame_number_from_pfn(pfn));
        fatal_error();
    }


    // Instead of storing this index in the pte, we store it in the pfn.
    // We do this because we now have larger indexes and can therefore have a larger paging file.
    // This works because we never have to access a disc index while accessing a frame number,
    // And we always want to access a frame number over a disc index
    // This allows us to extend the size of both variables instead of trying to cram them in a pte
    pfn->disc_index = disc_index;
    pfn->flags.state = STANDBY;

    EnterCriticalSection(&standby_page_list.lock);

    add_to_list(pfn, &standby_page_list, TRUE);

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
    // This thread needs to be able to react to handles for waking as well as exiting
    HANDLE handles[3];

    handles[0] = system_exit_event;
    handles[1] = modified_writing_event;
    handles[2] = disc_spot_available_event;

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

        /* The conditions that cause this to start running could become false
        Between when it is marked as ready and ran
        We always need to reevaluate this to see whether its actually needed
        * This is not important to actually fix as we will rewrite this in the near future (see below) */

        // This is an arbitrary condition that prompts us to write to disc when we need to
        // In the future we will give this thread a target of pages to write based on page demand
        while (modified_page_list.num_pages >= physical_page_count / 4)
        {
            if (write_page_to_disc() == FALSE)
            {
                break;
            }
        }
    }

    // This function doesn't actually return anything as it runs infinitely throughout the duration of the program
    return 0;
}

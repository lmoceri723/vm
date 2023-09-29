#include <stdio.h>
#include <windows.h>
#include <time.h>
#include "macros.h"
#include "structs.h"
#include "system.h"
#include "userapp.h"

#pragma comment(lib, "advapi32.lib")
#if 1
#define DBG                TRUE
#endif

// TODO LM FIX add todo to every LM Fix
// TODO LM FIX find the right version of %Iu to suppress warnings
// TODO LM FIX find a way to suppress unused parameter warnings
// TODO LM FIX ensure consistent conventions with style, including opening and closing {} curly brackets
// TODO LM FIX re-evaluate all ULONG_PTR variables on whether they should be ULONG64
// TODO LM FIX replace return false with fatal_error() when returning false corresponds to a fatal error

PPTE pte_from_va(PVOID virtual_address);
PVOID va_from_pte(PPTE pte);
PPFN pfn_from_frame_number(ULONG64 frame_number);

VOID trim(PPFN page);
PPFN get_free_page(VOID);
PPFN read_page_on_disc(PPTE pte, PPFN free_page);

VOID remove_from_list(PPFN pfn, BOOLEAN holds_locks);
VOID add_to_list(PPFN pfn, PPFN_LIST listhead, BOOLEAN holds_locks);
PPFN pop_from_list(PPFN_LIST listhead, BOOLEAN holds_locks);
VOID check_list_integrity(PPFN_LIST listhead, PPFN match_pfn);

VOID free_disc_space(ULONG64 disc_index);

VOID unlock_pte(PPTE pte);
VOID lock_pte(PPTE pte);
VOID unlock_pfn(PPFN pfn);
VOID lock_pfn(PPFN pfn);

ULONG_PTR virtual_address_size;
ULONG_PTR physical_page_count;
ULONG_PTR disc_page_count;
PFN_LIST free_page_list;
PFN_LIST modified_page_list;
PFN_LIST standby_page_list;
PPTE pte_base;
PVOID va_base;
PVOID modified_write_va;
PVOID modified_read_va;
PVOID repurpose_zero_va;
PVOID disc_space;
PUCHAR disc_in_use;
PUCHAR disc_end;
PPFN pfn_metadata;

// These functions convert between matching linear structures (pte and va) (pfn and frame number)
PPTE pte_from_va(PVOID virtual_address)
{
    // Null check done for security purposes
    if ((ULONG_PTR) virtual_address > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("pte_from_va : virtual address is out of valid range");
        return NULL;
    }

    // We can compute the difference between the first va and our va
    // This will be equal to the difference between the first pte and the pte we want
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    // The compiler automatically multiplies the difference by the size of a pte, so it is not required here
    return pte_base + difference;
}

// This method is the same as before, but it
PVOID va_from_pte(PPTE pte)
{
    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);
    difference *= PAGE_SIZE;

    PVOID result = (PVOID) ((ULONG_PTR) va_base + difference);

    // TODO LM FIX This null check should be done with the input, not the output
    if ((ULONG_PTR) result > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("va_from_pte : virtual address is out of valid range");
        return NULL;
    }
    return result;
}

// TODO add out of range checks here
PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    // Again, the compiler implicitly multiplies frame number by sizeof(PFN)
    return pfn_metadata + frame_number;
}

ULONG64 frame_number_from_pfn(PPFN pfn)
{
    return pfn - pfn_metadata;
}

// This breaks into the debugger if possible,
// Otherwise it crashes the program
// This is only done if our state machine is irreparably broken (or attacked)
VOID fatal_error(VOID)
{
    printf("\n fatal error");
    DebugBreak();
    exit(1);
}


// TODO read over this and write comments
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

    page->flags.state = MODIFIED;

    PPTE pte = page->pte;

    pte->memory_format.age = 0;
    pte->disc_format.valid = 0;

    EnterCriticalSection(&modified_page_list.lock);

    add_to_list(page, &modified_page_list, TRUE);

    LeaveCriticalSection(&modified_page_list.lock);
}

// TODO refine this by implementing the strategies below
//  Afterwards, read over this and write comments
// Age based on consumption
// Hop over pte regions, which will have both a bit-map and valid count / fancy skipping
// 33 bytes
// combine 0 and 256 by doing another read (try to see if 255 and 256 can be combined instead
VOID age_pages()
{
    ULONG64 pte_address;
    ULONG64 end_address;
    PPTE pte;
    PPFN pfn;

    for (ULONG64 region = 0; region < NUMBER_OF_PTE_REGIONS; region++)
    {
        pte_address = (ULONG64) pte_base + (region * PTE_REGION_SIZE * sizeof(PTE));
        end_address = pte_address + (PTE_REGION_SIZE * sizeof(PTE));

        lock_pte(pte);

        while (pte_address <= end_address)
        {
            pte = (PPTE) pte_address;
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

            pte_address += sizeof(PTE);
        }
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
        while (free_page_list.num_pages + standby_page_list.num_pages <= physical_page_count / 4)
        {
            age_pages();
        }
    }

    // This return statement only exists to satisfy the API requirements for a thread starting function
    return 0;
}


// This is how we get pages for new virtual addresses as well as old ones only exist on the paging file
PPFN get_free_page(VOID) {
    PPFN free_page = NULL;

    // This first check is done without a lock on the free page list
    // This is because the list is empty for the majority of the program's duration
    // We are able to check it faster without using the lock
    // This prevents us from waiting to access an empty list
    if (free_page_list.num_pages != 0) {
        EnterCriticalSection(&free_page_list.lock);
        // This check actually verifies that the list is empty
        if (free_page_list.num_pages != 0) {
            // TODO LM FIX once we allow users to free memory, we will need to zero this too
            free_page = pop_from_list(&free_page_list, TRUE);
        }
        LeaveCriticalSection(&free_page_list.lock);
    }

    // We do not check before getting a lock here like we do for the free list
    // We want to be 100% sure of our check here because we expect this case to happen almost every time
    // Also because we do not want to use our last resort option unless absolutely necessary

    // This is where we take pages from the standby list and reallocate their physical pages for our new va to use
    BOOLEAN took_standby_page = FALSE;
    EnterCriticalSection(&standby_page_list.lock);
    if ((free_page == NULL) && (standby_page_list.num_pages != 0)) {
        // TODO LM FIX call free disc space here
        free_page = pop_from_list(&standby_page_list, TRUE);

        LeaveCriticalSection(&standby_page_list.lock);
        took_standby_page = TRUE;

        // TODO LM ASK THIS IS TOTALLY A BUG
        //  This locking situation here is suspect
        PPTE pte = free_page->pte;
        // If this pte is in the same region as our first lock
        // The api will handle this by allowing the thread owner to claim and release the lock recursively
        lock_pte(pte);

        ULONG64 disc_index = free_page->disc_index;
        // This is where we clear the previous contents off of the repurposed page
        // This is important as it can corrupt the new user's data if not entirely overwritten,
        // It also would allow a program to see another program's memory (HUGE SECURITY VIOLATION)
        ULONG_PTR frame_number = frame_number_from_pfn(free_page);
        if (MapUserPhysicalPages(repurpose_zero_va, 1, &frame_number) == FALSE) {
            printf("page_fault_handler : could not map VA %p to page %lu\n", repurpose_zero_va,
                   frame_number);
            fatal_error();
        }

        memset(repurpose_zero_va, 0, PAGE_SIZE);

        if (MapUserPhysicalPages(repurpose_zero_va, 1, NULL) == FALSE) {
            printf("page_fault_handler : could not unmap VA %p to page %lu\n", repurpose_zero_va,
                   frame_number);
            fatal_error();
        }

        pte->disc_format.on_disc = 1;
        pte->disc_format.disc_index = disc_index;

        unlock_pte(pte);
    }
    // Exit the lock here instead of doing it earlier inside the function if there were no standby pages to take
    if (took_standby_page == FALSE)
    {
        LeaveCriticalSection(&standby_page_list.lock);
    }

    // This is a last resort option when there are no available pages
    // We wake the aging thread and send this information to the fault handler
    // Which then waits on a page to become available
    // Aging is done here no matter what
    SetEvent(wake_aging_event);
    if (free_page == NULL){

        return NULL;
    }
    // Once we have depleted a page from the free/standby list, it is a good idea to consider aging
    return free_page;
}

// This reads a page from the paging file and writes it back to memory
PPFN read_page_on_disc(PPTE pte, PPFN free_page)
{
    // We don't need a pfn lock here because this page is not on a list
    // And therefore is not visible to any other threads
    ULONG_PTR frame_number = frame_number_from_pfn(free_page);

    // We map these pages into our own va space to write contents into them, and then put them back in user va space
    if (MapUserPhysicalPages(modified_read_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        return NULL;
    }

    // This would be a disc driver that does this read and write in a real operating system
    PVOID source = (PVOID)  ((ULONG_PTR) disc_space + pte->disc_format.disc_index * PAGE_SIZE);
    memcpy(modified_read_va, source, PAGE_SIZE);

    if (MapUserPhysicalPages(modified_read_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        return NULL;
    }

    // Set the bit at disc_index in disc in use to be 0 to reuse the disc spot
    free_disc_space(pte->disc_format.disc_index);
    return free_page;
}

// TODO make this work with a 64 bit ULONG64
// This is very important, as it speeds this iteration up exponentially
// This is because the CPU will now be able to check 64 spots instead of 8 (char size spots) in a single register
ULONG get_disc_index(VOID)
{
    PUCHAR disc_spot;
    ULONG disc_index;
    UCHAR spot_cluster;

    EnterCriticalSection(&disc_in_use_lock);

    // Reads each char of our bit map and breaks when it finds a non-full char
    disc_spot = disc_in_use;
    while (disc_spot != disc_end)
    {
        // 0xFF is equal to a char with all bits set
        if (*disc_spot != 0xFF)
        {
            break;
        }
        disc_spot++;
    }

    // In this case, no disc spots are available
    // This returns to the modified_write_thread, which waits for a disc spot to be free
    if (disc_spot == disc_end)
    {
        LeaveCriticalSection(&disc_in_use_lock);
        return MAXULONG32;
    }

    // This reads in the char (byte) with an empty slot
    spot_cluster = *disc_spot;
    disc_index = 0;
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
            // We then left shift this value by disc_index bits
            // In order to match the positions of our two bits
            *disc_spot |= (1<<disc_index);
            break;
        }
        disc_index++;
        // This throws away the rightmost lowest bit, shifts every bit to the right, and the highest bit is set to zero
        // This is safe to do as we know that one of the 8 bits we are checking will be a zero
        // So we will never get to this pocket of zeroes at the end
        // We use this to always have the bit we want to check as the first bit
        spot_cluster = spot_cluster>>1;
    }

    LeaveCriticalSection(&disc_in_use_lock);
    // This returns the exact index inside disc_in_use that corresponds to the now occupied disc spot
    return (disc_spot - disc_in_use) * sizeof(char) + disc_index;
}

// TODO also make this work with a 64 bit ULONG64
VOID free_disc_space(ULONG64 disc_index)
{
    EnterCriticalSection(&disc_in_use_lock);

    // This grabs the actual char (byte) that holds the bit we need to change
    PUCHAR disc_spot = disc_in_use + disc_index / sizeof(char);
    UCHAR spot_cluster = *disc_spot;

    // This gets the bit's index inside the char
    ULONG index_in_char = disc_index % sizeof(char);

    // We set the bit to be zero by comparing it using a LOGICAL AND (&=) with all ones,
    // Except for a zero at the place we want to set as zero.
    // We compute this comparison value by taking one positive bit (1)
    // And left-shifting (<<) it by index_in_char bits to its corresponding position in the char
    // If the position is two, then our 1 would become 001. Five more bits would then be added to the end
    // By the compiler in order to match the size of the char it is being compared to (00100000)
    // Then we flip these bits using the (~) operator to get our comparison value

    // Example: (actual byte) 11011101 &= 11111011 (comparison value)
    // Result: 11011(0)01
    // The zero surrounded by parenthesis is the bit we change; all others are preserved

    spot_cluster &= ~(1<<(index_in_char));

    // Write the char back out after the bit has been changed
    *disc_spot = spot_cluster;

    LeaveCriticalSection(&disc_in_use_lock);
    SetEvent(disc_spot_available_event);
}

// TODO fix/rewrite this function and write comments
// This function takes the first modified page and writes it to the disc
BOOLEAN write_page_to_disc(VOID)
{
    PPFN pfn;
    ULONG_PTR frame_number;

    pfn = pop_from_list(&modified_page_list, FALSE);

    lock_pfn(pfn);

    frame_number = frame_number_from_pfn(pfn);

    // We need to map the page into our own va private space to copy its contents
    if (MapUserPhysicalPages (modified_write_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_write_va,
                frame_number_from_pfn(pfn));
        fatal_error();
    }

    ULONG disc_index = get_disc_index();

    // At this point, we cannot get a disc index, so we put the page back on the modified list and wait for a spot
    // This is a last resort option, as we have to completely undo everything and try again
    if (disc_index == MAXULONG32)
    {
        if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_write_va,
                    frame_number_from_pfn(pfn));
            fatal_error();
        }

        EnterCriticalSection(&modified_page_list.lock);

        add_to_list(pfn, &modified_page_list, TRUE);

        LeaveCriticalSection(&modified_page_list.lock);

        unlock_pfn(pfn);

        // This signifies to the calling function that we failed
        return FALSE;
    }

    // This computes the actual address of where we want to write the page contents in the paging file and copies it
    PVOID actual_space;
    actual_space = (PVOID) ((ULONG_PTR) disc_space + (disc_index * PAGE_SIZE));
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
    pfn->flags.state = CLEAN;

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
    HANDLE handles[2];

    handles[0] = system_exit_event;
    handles[1] = modified_writing_event;

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
        This is not important to actually fix as we will rewrite this in the near future (see below) */

        // TODO LM Fix/Ask can the trim always get what we want? Yes? (Ask Landy for clarification)

        // This is an arbitrary condition that prompts us to write to disc when we need to
        // In the future we will give this thread a target of pages to write based on page demand
        while (modified_page_list.num_pages >= physical_page_count / 4)
        {
            if (write_page_to_disc() == FALSE)
            {
                // TODO LM Fix we need to have an event here to wait for a disc spot like we do
                //  When free_page == NULL in the page fault handler.
                //  Put its set event in free_disc_spot
                WaitForSingleObject(disc_spot_available_event, INFINITE);
                break;
            }
        }
    }

    // This function doesn't actually return anything as it runs infinitely throughout the duration of the program
    return 0;
}

// This removes the first element from a pfn list and returns it
PPFN pop_from_list(PPFN_LIST listhead, BOOLEAN holds_locks)
{
    PPFN pfn;
    PLIST_ENTRY flink_entry;

    if (holds_locks == FALSE)
    {
        EnterCriticalSection(&listhead->lock);
    }

    check_list_integrity(listhead, NULL);

    flink_entry = RemoveHeadList(&listhead->entry);
    pfn = CONTAINING_RECORD(flink_entry, PFN, entry);

    listhead->num_pages--;

    check_list_integrity(listhead, NULL);

    if (holds_locks == FALSE)
    {
        LeaveCriticalSection(&listhead->lock);
    }
    return pfn;
}

// Removes the pfn from the list corresponding to its state
VOID remove_from_list(PPFN pfn, BOOLEAN holds_locks)
{
    PPFN_LIST listhead;
    PLIST_ENTRY prev_entry;
    PLIST_ENTRY next_entry;


    // Takes the necessary locks if asked by the caller
    if (holds_locks == FALSE) {
        lock_pfn(pfn);
    }

    // Finds the list based on the page's state
    if (pfn->flags.state == MODIFIED) {

        listhead = &modified_page_list;

    } else if (pfn->flags.state == CLEAN) {

        // This is on the standby list
        listhead = &standby_page_list;
    } else {

        printf("Tried to remove a page from list when it was on none");
        fatal_error();
        return;
    }

    if (holds_locks == FALSE)
    {
        EnterCriticalSection(&listhead->lock);
    }

    // Checks the integrity of the list before and after the remove operation
    check_list_integrity(listhead, pfn);

    // This removes the page from the list by erasing it from the chain of flinks and blinks
    prev_entry = pfn->entry.Blink;
    next_entry = pfn->entry.Flink;

    prev_entry->Flink = next_entry;
    next_entry->Blink = prev_entry;

    pfn->entry.Flink = NULL;
    pfn->entry.Blink = NULL;

    listhead->num_pages--;

    check_list_integrity(listhead, NULL);

    if (holds_locks == FALSE)
    {
        LeaveCriticalSection(&listhead->lock);
        unlock_pfn(pfn);
    }
}

// This function simply adds a pfn to a specified list
VOID add_to_list(PPFN pfn, PPFN_LIST listhead, BOOLEAN holds_locks) {

    // Takes the necessary locks if asked by the caller
    if (holds_locks == FALSE) {
        lock_pfn(pfn);
        EnterCriticalSection(&listhead->lock);
    }

    // Inserts it on the list, checking the integrity of it before and after
    check_list_integrity(listhead, NULL);
    InsertTailList(&listhead->entry, &pfn->entry);
    listhead->num_pages++;
    check_list_integrity(listhead, pfn);

    if (holds_locks == FALSE) {
        LeaveCriticalSection(&listhead->lock);
        unlock_pfn(pfn);
    }
}

// Functions to lock and unlock pte regions and individual pfns
VOID lock_pte(PPTE pte)
{
    // We do not need to cast or multiply/divide by the size of a pte
    // This is because the compiler is smart enough to do this for us
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

    EnterCriticalSection(&pte_region_locks[index]);
}

VOID unlock_pte(PPTE pte)
{
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

    LeaveCriticalSection(&pte_region_locks[index]);
}

VOID lock_pfn(PPFN pfn)
{
    EnterCriticalSection(&pfn->flags.lock);
}

VOID unlock_pfn(PPFN pfn)
{
    LeaveCriticalSection(&pfn->flags.lock);
}


// Checks the integrity of a pfn list
// This is very helpful to use when debugging but very expensive to do
// This is done when a thread either acquires or is about to release a lock on a pfn list
VOID check_list_integrity(PPFN_LIST listhead, PPFN match_pfn)
{
#if DBG
    PLIST_ENTRY flink_entry;
    PPFN pfn;
    ULONG state;
    ULONG count;
    ULONG matched;

    // This makes sure that this list is actually owned by the calling thread
    if (listhead->lock.OwningThread == NULL)
    {
        // Because this is only meant to be run while debugging, we break into the debugger instead of crashing
        DebugBreak();
    }

    // We start at the first entry on our list and iterate to the end of it
    flink_entry = listhead->entry.Flink;
    matched = 0;
    count = 0;

    // If we have a matching pfn, we pull the state from that pfn to compare to other pages
    if (match_pfn != NULL)
    {
        state = match_pfn->flags.state;
    }
    // Otherwise, we grab the state from the first page on the list
    else
    {
        // Grabs the entire pfn surrounding the LIST_ENTRY instance variable
        state = CONTAINING_RECORD(flink_entry, PFN, entry)->flags.state;
    }

    // This iterates over each page, ensuring that it meets all of our checks
    while (flink_entry != &listhead->entry) {

        // Grabs the pfn structure from flink_entry and then sets flink_entry to point at the flink of flink_entry
        pfn = CONTAINING_RECORD(flink_entry, PFN, entry);
        flink_entry = pfn->entry.Flink;

        // This attempts to check if all pfns on the list share a state
        if (pfn->flags.state != state) {
            DebugBreak();
        }

        // This counts how many times match_pfn is found in our list
        if (match_pfn != NULL && pfn == match_pfn) {
            matched++;
        }

        count++;
    }

    // Finally, we check if the list's count is broken
    if (count != listhead->num_pages) {
        DebugBreak();
    }

    // This occurs when we have not found our matching pfn in the list or have found it twice
    if (match_pfn != NULL && matched != 1) {
        DebugBreak();
    }
#endif
}

// This is where we handle any access or fault of a page
VOID page_fault_handler(PVOID arbitrary_va)
{
    // Pages go through the handler regardless of whether they have faulted or not
    // This is because even if a page is accessed without a fault, it's age in the pte must be updated

    // First, we need to get the actual pte corresponding to the va we faulted on
    PPTE pte = pte_from_va(arbitrary_va);
    if (pte == NULL)
    {
        printf("page_fault_handler : cannot get pte from va");
        fatal_error();
    }

    // This order of operations is very important
    // A pte lock MUST sequentially come before a pfn lock
    // This is because we must lock the pte corresponding to a faulted va in order to handle its fault
    // At this point we do not know the pfn and cannot find it without a pte lock
    lock_pte(pte);
    PTE pte_contents = *pte;

    // This is where the age is updated on an active page that has not actually faulted
    // We refer to this as a fake fault
    // TODO LM ASK Landy is this more efficient than writing it to zero every time
    if (pte_contents.memory_format.valid == 1)
    {
        fake_faults++;
        if (pte->memory_format.age == 0)
        {
            unlock_pte(pte);
            return;
        }
        else
        {
            pte->memory_format.age = 0;
        }

        unlock_pte(pte);
        return;
    }

    // At this point, we know that the page actually faulted
    num_faults++;

    PPFN pfn;

    // If the frame number is zero, it means that this is a brand new va that has never been accessed.
    // Inversely, we know that a va has been accessed if the frame number is non-zero
    // We need to get a free/standby page and map it to this va
    if (pte_contents.memory_format.frame_number == 0) {

        pfn = get_free_page();

        // This occurs when we get_free_page fails to find us a free page
        // When this happens, we release our lock on this pte and wait for pages to become available
        // Once we are able to map a page to this va, we return, which lets the thread fault on this va again
        if (pfn == NULL) {
            unlock_pte(pte);
            WaitForSingleObject(pages_available_event, INFINITE);
            return;
        }

        // TODO LM FIX move this above
        // If the frame number of a pte is PAGE_ON_DISC, it means that this page only exists in the paging file
        // It has been trimmed, written to disc, and its physical page has been reused
        // We need to read this page from the paging file and remap it to the va
        // This is called a hard fault, as we have to read from disc in order to handle it
        // We want to minimize this type of fault, as it takes exponentially longer than other types of faults
    } else if (pte_contents.disc_format.on_disc == 1) {

        pfn = get_free_page();
        if (pfn == NULL) {
            unlock_pte(pte);
            WaitForSingleObject(pages_available_event, INFINITE);
            return;
        }

        // This is where we actually read the page from the disc and write its contents to our new page
        read_page_on_disc(pte, pfn);

        // We know now that this va must have been trimmed and not yet written to disc
        // It either exists on the modified or standby list
        // All we need to do is remove it from these lists now
        // This is called a soft fault, as this can be done all in memory and doesn't require disc reads/writes
        // This is our ideal type of fault, as it takes the shortest amount of time
    } else {

        // This will unlink our page from the standby or modified list
        // It uses the pfn's information to determine which list it is on
        pfn = pfn_from_frame_number(pte_contents.memory_format.frame_number);
        // TODO LM FIX where are we calling free disc space
        remove_from_list(pfn, FALSE);
    }

    // This lock is needed in order to safely ensure we can change the pfn
    lock_pfn(pfn);

    // We now have the page we need, we just need to correctly map it now to the va
    pte->memory_format.frame_number = frame_number_from_pfn(pfn);
    pte->memory_format.valid = 1;
    pte->memory_format.on_disc = 0;
    pte->memory_format.age = 0;

    pfn->pte = pte;
    pfn->flags.state = ACTIVE;

    // This is a Windows API call that confirms the changes we made with the OS
    // We have already mapped this va to this page on our side, but the OS also needs to do the same on its side
    // This is necessary as this is a user mode program
    ULONG_PTR frame_number = frame_number_from_pfn(pfn);
    if (MapUserPhysicalPages(arbitrary_va, 1, &frame_number) == FALSE) {

        printf("page_fault_handler : could not map VA %p to page %llX\n", arbitrary_va,
               frame_number_from_pfn(pfn));
        fatal_error();
    }

    unlock_pfn(pfn);
    unlock_pte(pte);
}

// Eventually, we will move this to an api.c and api.h file
// That way programs can use our memory manager without knowing or having to know anything about how it works
PVOID allocate_memory(PULONG_PTR num_bytes)
{
    *num_bytes = virtual_address_size;
    return va_base;
}

// This main is likely to be moved to userapp.c in the future
int main (int argc, char** argv)
{
     /* This is where we initialize and test our virtual memory management state machine

     We control the entirety of virtual and physical memory management with only two exceptions
        We ask the operating system for all physical pages we use to store data
        (AllocateUserPhysicalPages)
        We ask the operating system to connect one of our virtual addresses to one of our physical pages
        (MapUserPhysicalPages)

     In a real kernel program, we would do these things ourselves,
     But the operating system (for security reasons) does not allow us to.

     But we do everything else commonly features in a memory manager
     Including: maintaining translation tables, PTE and PFN data structures, management of physical pages,
     Virtual memory operations like handling page faults, materializing mappings, freeing them, trimming them,
     Writing them out to a paging file, bringing them back from the paging file, protecting them, and much more */

    if (initialize_system() == FALSE) {
        printf("main : could not initialize system");
        return 1;
    }

    if (full_virtual_memory_test() == FALSE)
    {
        printf("main : full virtual memory test failed");
        return 1;
    }

    deinitialize_system();
    return 0;
}

#include <stdio.h>
#include <windows.h>
#include <time.h>
#include "macros.h"
#include "structs.h"
#include "system.h"
#include "userapp.h"

#pragma comment(lib, "advapi32.lib")

PPTE pte_from_va(PVOID virtual_address);
PVOID va_from_pte(PPTE pte);
PPFN pfn_from_frame_number(ULONG64 frame_number);
//VOID clear_standby_list();
BOOLEAN trim(PPFN page);
PPFN get_free_page(VOID);
PPFN read_page_on_disc(PPTE pte, PPFN free_page);
VOID remove_from_list(PPFN pfn, BOOLEAN holds_locks);
VOID free_disc_space(ULONG64 disc_index);

ULONG_PTR virtual_address_size;
ULONG_PTR physical_page_count;
ULONG_PTR disc_page_count;
PFN_LIST free_page_list;
PFN_LIST modified_page_list;
PFN_LIST standby_page_list;
PFN_LIST active_page_list[NUMBER_OF_AGES];
PPTE pte_base;
PVOID va_base;
PVOID modified_write_va;
PVOID modified_read_va;
PVOID disc_space;
PUCHAR disc_in_use;
PUCHAR disc_end;
PPFN pfn_metadata;

PPTE pte_from_va(PVOID virtual_address)
{
    if ((ULONG_PTR) virtual_address > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("pte_from_va : virtual address is out of valid range");
        return NULL;
    }
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    return pte_base + difference;
}

PVOID va_from_pte(PPTE pte)
{
    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);
    difference *= PAGE_SIZE;

    PVOID result = (PVOID) ((ULONG_PTR) va_base + difference);
    if ((ULONG_PTR) result > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("va_from_pte : virtual address is out of valid range");
        return NULL;
    }
    return result;
}

// LM Fix check for out of range
PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    return pfn_metadata + frame_number;
}

ULONG64 frame_number_from_pfn(PPFN pfn)
{
    return pfn - pfn_metadata;
}

#if 0
VOID clear_standby_list()
{
    while (standby_page_list.num_pages != 0)
    {
        // Update the pte
        PPFN pfn = (PPFN) RemoveHeadList(&standby_page_list.entry);
        pfn->state = FREE;
        standby_page_list.num_pages--;
        InsertTailList(&free_page_list.entry, &pfn->entry);
        free_page_list.num_pages++;
    }
}
#endif

VOID fatal_error(VOID)
{
    DebugBreak();
    exit(1);
}

// LM Multi
// This puts a page on the modified list
BOOLEAN trim(PPFN page)
{
    PVOID user_va = va_from_pte(page->pte);
    if (user_va == NULL)
    {
        // FATAL
        printf("trim : could not get the va connected to the pte");
        fatal_error();
    }

    // The user va is still mapped, we need to unmap it here to stop the user from changing it
    // Any attempt to modify this va will lead to a page fault so that we will not be able to have stale data
    if (MapUserPhysicalPages (user_va, 1, NULL) == FALSE) {

        // FATAL
        printf ("trim : could not unmap VA %p to page %llX\n", user_va, frame_number_from_pfn(page));
        fatal_error();
    }

    remove_from_list(page, TRUE);

    page->flags.state = MODIFIED;
    page->pte->hardware_format.valid = 0;

    EnterCriticalSection(&modified_page_list.lock);

    InsertTailList(&modified_page_list.entry, &page->entry);
    modified_page_list.num_pages++;

    LeaveCriticalSection(&modified_page_list.lock);

    return TRUE;
}

// LM Multi
// Age based on consumption
BOOLEAN age_pages()
{
    PLIST_ENTRY flink_entry;
    PPFN pfn;

    for (unsigned i = 0; i < NUMBER_OF_AGES; i++)
    {
        unsigned age = NUMBER_OF_AGES - i - 1;

        EnterCriticalSection(&pfn_lock);
        EnterCriticalSection(&active_page_list[age].lock);

        flink_entry = active_page_list[age].entry.Flink;
        while (flink_entry != &active_page_list[age].entry)
        {
            // We deliberately capture the Flink now
            // The page might enter the modified list and be assigned a new one

            pfn = CONTAINING_RECORD(flink_entry, PFN, entry);
            flink_entry = pfn->entry.Flink;

            if (age == NUMBER_OF_AGES - 1)
            {
                if (trim(pfn) == FALSE)
                {
                    printf("age_pages : could not trim page");
                    LeaveCriticalSection(&active_page_list[age].lock);
                    LeaveCriticalSection(&pfn_lock);
                    return FALSE;
                }

                SetEvent(modified_writing_event);
            }
            else
            {
                // LM Fix make swapping active lists a function
                EnterCriticalSection(&active_page_list[age+1].lock);

                remove_from_list(pfn, TRUE);
                pfn->flags.age++;
                InsertTailList(&active_page_list[age+1].entry, &pfn->entry);

                LeaveCriticalSection(&active_page_list[age+1].lock);

            }
        }
        //TryEnterCriticalSection()
        LeaveCriticalSection(&active_page_list[age].lock);
        LeaveCriticalSection(&pfn_lock);
    }
    return TRUE;
}

// Nobody gets to call this, it needs to be invoked in its own thread context
DWORD trim_thread(PVOID context)
{
    while (TRUE)
    {
        // The conditions that cause this to start running could become false between when it is marked as ready and ran
        // We always need to reevaluate this to see whether its actually needed
        if (free_page_list.num_pages <= physical_page_count / 4)
        {
            // We are not just looking the other way here, we have a trust model
            age_pages();
        }

        // wait for memory running low object as well as system exit object
        WaitForSingleObject(wake_aging_event, INFINITE);
    }
}


PPFN get_free_page(VOID) {

    PPFN free_page;

    if (free_page_list.num_pages != 0) {
        EnterCriticalSection(&free_page_list.lock);
        // LM Fix do this for the others
        if (free_page_list.num_pages != 0){
            free_page = (PPFN) RemoveHeadList(&free_page_list.entry);
            free_page_list.num_pages--;
        }
        LeaveCriticalSection(&free_page_list.lock);
    } else if (standby_page_list.num_pages != 0) {
        EnterCriticalSection(&standby_page_list.lock);
        free_page = (PPFN) RemoveHeadList(&standby_page_list.entry);
        PPTE pte = free_page->pte;

        // Make this zero so a subsequent fault on this VA will know to get this from disc
        // This needs to be protected by the pte read lock, right now it is being done from the handler
        pte->software_format.frame_number = PAGE_ON_DISC;
        standby_page_list.num_pages--;
        LeaveCriticalSection(&standby_page_list.lock);
    } else {
        // Ideally this is a last resort option and not something of high frequency
        // LM Fix, this would be better suited to be its own thread running find_trim_candidates
        // Instead we want to wait for an event for this thread after returning a failure status
        // We would want to release our pte read lock as if we failed

        //printf("get_free_page : could not find trim candidates");
        SetEvent(wake_aging_event);
        return NULL;
    }
        // Be conscious of whether we need a lock here
        free_page->flags.state = ACTIVE;
        return free_page;
}

PPFN read_page_on_disc(PPTE pte, PPFN free_page)
{
    // LM Fix we will want to fix this to not hold the pte read lock
    if (free_page == NULL)
    {
        printf("read_page_on_disc : could not get a free page");
        return NULL;
    }

    ULONG_PTR frame_number = frame_number_from_pfn(free_page);
    if (MapUserPhysicalPages(modified_read_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        return NULL;
    }

    // This would be a disc driver that does this read in a real operating system
    PVOID source = (PVOID)  ((ULONG_PTR) disc_space + pte->software_format.disc_index * PAGE_SIZE);
    memcpy(modified_read_va, source, PAGE_SIZE);

    if (MapUserPhysicalPages(modified_read_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        return NULL;
    }

    // Set the char at disc_index in disc in use to be 0 to avoid leaking disc space and cause a clog up of the machine
    free_disc_space(pte->software_format.disc_index);
    return free_page;
}

// LM Fix make the two functions below work with a ULONG64 instead of a char
// LM Multi
ULONG get_disc_index(VOID)
{
    EnterCriticalSection(&disc_in_use_lock);
    PUCHAR disc_spot = disc_in_use;
    // LM Fix this runtime doesn't fly anymore
    while (disc_spot != disc_end)
    {
        if (*disc_spot != 0xFF)
        {
            break;
        }
        disc_spot++;
    }

    if (disc_spot == disc_end)
    {
        return MAXULONG32;
    }
    ULONG disc_index = 0;
    UCHAR spot_cluster = *disc_spot;
    while (TRUE)
    {
        // If the low bit is a one then the spot is in use, if not it is free to use and we break
        if ((spot_cluster & 0x1) == 0)
        {
            // set the bit and return right disc index at the end of function
            *disc_spot |= (1<<disc_index);
            break;
        }
        disc_index++;
        // Throws away the rightmost lowest bit, shifts to the right and zero fills the high bit
        spot_cluster = spot_cluster>>1;
    }
    LeaveCriticalSection(&disc_in_use_lock);
    return (disc_spot - disc_in_use) * 8 + disc_index;
}

VOID free_disc_space(ULONG64 disc_index)
{
    EnterCriticalSection(&disc_in_use_lock);
    PUCHAR disc_spot = disc_in_use + disc_index / 8;
    // Read in the byte
    UCHAR spot_cluster = *disc_spot;
    // We set the bit to be zero by comparing it using an and with all 1s except for a zero at the place we want to set as zero
    // For example 11011101 & 11111011
    // The zero is the bit we change, all others are preserved
    spot_cluster &= ~(1<<(disc_index % 8));
    // Write the byte out
    *disc_spot = spot_cluster;
    LeaveCriticalSection(&disc_in_use_lock);
}

VOID write_page_to_disc(VOID)
{
    EnterCriticalSection(&pfn_lock);
    EnterCriticalSection(&modified_page_list.lock);

    PPFN pfn = (PPFN) RemoveHeadList(&modified_page_list.entry);
    modified_page_list.num_pages--;

    LeaveCriticalSection(&modified_page_list.lock);

    ULONG_PTR frame_number = frame_number_from_pfn(pfn);
    if (MapUserPhysicalPages (modified_write_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_write_va,
                frame_number_from_pfn(pfn));
        fatal_error();
    }

    ULONG disc_index = get_disc_index();

    // At this point we cannot get a disc index
    if (disc_index == MAXULONG32)
    {
        if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_write_va,
                    frame_number_from_pfn(pfn));
            fatal_error();
        }

        EnterCriticalSection(&modified_page_list.lock);

        InsertHeadList(&modified_page_list.entry, &pfn->entry);
        modified_page_list.num_pages++;

        LeaveCriticalSection(&modified_page_list.lock);
        // Even though we don't write here, we return true, so we can try it again later
        return;
    }

    PVOID actual_space;
    actual_space = (PVOID) ((ULONG_PTR) disc_space + (disc_index * PAGE_SIZE));

    memcpy(actual_space, modified_write_va, PAGE_SIZE);

    pfn->pte->software_format.disc_index = disc_index;

    if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_write_va,
                frame_number_from_pfn(pfn));
        fatal_error();
    }
    pfn->flags.state = CLEAN;

    EnterCriticalSection(&standby_page_list.lock);

    InsertTailList(&standby_page_list.entry, &pfn->entry);
    standby_page_list.num_pages++;

    LeaveCriticalSection(&standby_page_list.lock);
    LeaveCriticalSection(&pfn_lock);
}

DWORD modified_write_thread(PVOID context)
{
    while (TRUE)
    {
        // The conditions that cause this to start running could become false between when it is marked as ready and ran
        // We always need to reevaluate this to see whether its actually needed
        while (modified_page_list.num_pages >= physical_page_count / 4)
        {
            // We are not just looking the other way here, we have a trust model
            write_page_to_disc();
        }

        // wait for memory running low object as well as system exit object
        WaitForSingleObject(modified_writing_event, INFINITE);
    }
}


VOID remove_from_list(PPFN pfn, BOOLEAN holds_locks)
{
    // LM Fix do all callers need to be true?
    // Removes the pfn from whatever list its on, does not matter which list it is
    if (holds_locks == FALSE) {
        EnterCriticalSection(&pfn_lock);
    }
    PPFN_LIST listhead;

    if (pfn->flags.state == MODIFIED)
    {
        listhead = &modified_page_list;
    }
    else if (pfn->flags.state == ACTIVE)
    {
        listhead = &active_page_list[pfn->flags.age];
    }
    else
    {
        // This is on the standby list
        listhead = &standby_page_list;
        free_disc_space(pfn->pte->software_format.disc_index);
        pfn->pte->software_format.disc_index = 0;
    }
    if (holds_locks == FALSE)
    {
        EnterCriticalSection(&listhead->lock);
    }

    PLIST_ENTRY prev_entry = pfn->entry.Blink;
    PLIST_ENTRY next_entry = pfn->entry.Flink;

    prev_entry->Flink = next_entry;
    next_entry->Blink = prev_entry;

    listhead->num_pages--;

    if (holds_locks == FALSE)
    {
        LeaveCriticalSection(&listhead->lock);
        LeaveCriticalSection(&pfn_lock);
    }
}

VOID page_fault_handler(BOOLEAN faulted, PVOID arbitrary_va)
{
    // This now needs to be done whether a fault occurs or not in order to update a pages age
    PPTE pte = pte_from_va(arbitrary_va);
    if (pte == NULL)
    {
        printf("page_fault_handler : cannot get pte from va");
        return FALSE;
    }
    // LM FIX Correctly initialize this lock and refactor to change the name to pte_lock
    EnterCriticalSection(&pte_read_lock);
    PTE pte_contents = *pte;

    // If this page has not faulted we still need to update its age and return
    if (faulted == FALSE)
    {
        PPFN pfn;
        pfn = pfn_from_frame_number(pte_contents.hardware_format.frame_number);
        fake_faults++;
        if (pfn->flags.age == 0)
        {
            LeaveCriticalSection(&pte_read_lock);
            return TRUE;
        }

        // LM FIX Do the same here but with this lock instead
        EnterCriticalSection(&pfn_lock);
        ULONG age = pfn->flags.age;
        EnterCriticalSection(&active_page_list[0].lock);
        EnterCriticalSection(&active_page_list[age].lock);

        if (pfn->flags.age != 0) {

            pfn->flags.age = 0;

            remove_from_list(pfn, TRUE);
            InsertTailList(&active_page_list[0].entry, &pfn->entry);
        }

        EnterCriticalSection(&active_page_list[age].lock);
        LeaveCriticalSection(&active_page_list[0].lock);
        LeaveCriticalSection(&pfn_lock);
        LeaveCriticalSection(&pte_read_lock);
        return TRUE;
    }
    num_faults++;
    // Connect the virtual address now - if that succeeds then
    // we'll be able to access it from now on.

    // we need to get the pte from arbitrary va f(va)->pte

    PPFN pfn;

    // Get a free page if there is no address in the pte
    if (pte_contents.software_format.frame_number == 0) {
    // This virtual address has never been accessed
        pfn = get_free_page();
        if (pfn == NULL) {
            LeaveCriticalSection(&pte_read_lock);
            printf("page_fault_handler : could not get a page");
            return FALSE;
        }
    } else if (pte_contents.software_format.frame_number == PAGE_ON_DISC) {
    // This virtual address has been previously accessed and its contents now exclusively exists on disc
    // It has been trimmed, written to disc, and its physical page has been reused.
    // Page on disc gets wiped out when we rewrite the frame number below
        pfn = get_free_page();
        if (pfn == NULL) {
            LeaveCriticalSection(&pte_read_lock);
            printf("page_fault_handler : could not get a page");
            return FALSE;
        }
        read_page_on_disc(pte, pfn);
    } else {
    // This is an address that has been accessed and trimmed
    // This will unlink it from the standby or modified list
        pfn = pfn_from_frame_number(pte_contents.software_format.frame_number);
        remove_from_list(pfn, FALSE);
    }

    // We have the page we need, now we need to mark it as active and map it
    pte->hardware_format.frame_number = frame_number_from_pfn(pfn);
    pte->hardware_format.valid = 1;

    // Acquiring this lock so that the pfn's age is not changed by aging or trimming threads
    EnterCriticalSection(&pfn_lock);
    ULONG age = pfn->flags.age;
    EnterCriticalSection(&active_page_list[0].lock);
    EnterCriticalSection(&active_page_list[age].lock);

    InsertTailList(&active_page_list[pfn->flags.age].entry, &pfn->entry);
    active_page_list[pfn->flags.age].num_pages++;

    pfn->pte = pte;
    pfn->flags.age = 0;
    pfn->flags.state = ACTIVE;

    LeaveCriticalSection(&active_page_list[age].lock);
    LeaveCriticalSection(&active_page_list[0].lock);

    ULONG_PTR frame_number = frame_number_from_pfn(pfn);
    if (MapUserPhysicalPages(arbitrary_va, 1, &frame_number) == FALSE) {

        LeaveCriticalSection(&pfn_lock);
        LeaveCriticalSection(&pte_read_lock);
        printf("page_fault_handler : could not map VA %p to page %llX\n", arbitrary_va,
               frame_number_from_pfn(pfn));
        return FALSE;
    }
    LeaveCriticalSection(&pfn_lock);
    LeaveCriticalSection(&pte_read_lock);

    /* No exception handler needed now since we have connected
    the virtual address above to one of our physical pages
    so no subsequent fault can occur. */

    return TRUE;
}
// Eventually move to api.c file
PVOID allocate_memory(PULONG_PTR num_bytes)
{
    *num_bytes = virtual_address_size;
    return va_base;
}

int 
main (int argc, char** argv)
{
     /*   Test our very complicated user mode virtual implementation.

     We will control the virtual and physical address space management
     ourselves with the only two exceptions being that we will :

     1. Ask the operating system for the physical pages we'll use to
        form our pool.

     2. Ask the operating system to connect one of our virtual addresses
        to one of our physical pages (from our pool).

     We would do both of those operations ourselves but the operating
     system (for security reasons) does not allow us to.

     But we will do all the heavy lifting of maintaining translation
     tables, PFN data structures, management of physical pages,
     virtual memory operations like handling page faults, materializing
     mappings, freeing them, trimming them, writing them out to backing
     store, bringing them back from backing store, protecting them, etc.

     This is where we can be as creative as we like, the sky's the limit ! */
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

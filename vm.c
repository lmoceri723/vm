#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "conversions.h"
#include "system.h"

// sxd av to ignore all page faults when trying to write to arbitrary va
// Second chance is an actual exception separate from a page fault

#pragma comment(lib, "advapi32.lib")

// #define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

PPTE pte_from_va(PVOID virtual_address);
PVOID va_from_pte(PPTE pte);
PPFN pfn_from_frame_number(ULONG64 frame_number);
VOID write_modified_pages();
//VOID clear_standby_list();
VOID find_trim_candidates();
VOID trim(PPFN page);
//VOID age(PPFN page);
PPFN get_free_page(VOID);
PPFN read_page_on_disc(PPTE pte);
VOID remove_from_list(PPFN pfn);

ULONG_PTR virtual_address_size;
ULONG_PTR physical_page_count;
ULONG_PTR disc_page_count;
PFN_LIST free_page_list;
PFN_LIST modified_page_list;
PFN_LIST standby_page_list;
PPTE pte_base;
PPTE pte_end;
PVOID va_base;
PVOID modified_write_va;
PVOID modified_read_va;
PVOID disc_space;
PUCHAR disc_in_use;
PUCHAR disc_end;
PPFN pfn_metadata;


PPTE pte_from_va(PVOID virtual_address)
{
    // LM Fix check that a va is in bounds, if it is return null
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    return pte_base + difference;
}

PVOID va_from_pte(PPTE pte)
{
    // ULONG_PTR difference = (ULONG_PTR) pte - (ULONG_PTR) pte_base;

    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);

    difference *= PAGE_SIZE;

    return (PVOID) ((ULONG_PTR) va_base + difference);
}

PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    PPFN pfn = pfn_metadata;
    for (ULONG_PTR i = 0; i < physical_page_count; i++)
    {
        if (pfn->frame_number == frame_number)
        {
            return pfn;
        }
        pfn++;
    }
    //LM Fix Fatal error
    return NULL;
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

// This puts a page on the modified list
VOID trim(PPFN page)
{
    PVOID user_va = va_from_pte(page->pte);

    // The user va is still mapped, we need to unmap it here to stop the user from changing it
    // Any attempt to modify this va will lead to a page fault so that we will not be able to have stale data
    if (MapUserPhysicalPages (user_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %lX\n", user_va, page->frame_number);
        return;
    }

    page->state = MODIFIED;
    page->pte->hardware_format.valid = 0;

    InsertTailList(&modified_page_list.entry, &page->entry);
    modified_page_list.num_pages++;
}

VOID find_trim_candidates()
{
    PPTE pte_pointer = pte_base;

    while (pte_pointer != pte_end)
    {
        PTE contents = *pte_pointer;
        if (contents.hardware_format.valid)
        {
            // We have a candidate, now we need a pfn to trim it
            PPFN pfn = pfn_from_frame_number(contents.hardware_format.frame_number);
            trim(pfn);
        }
        pte_pointer++;
    }

    write_modified_pages();
}

#if 0
VOID age(PPFN page)
{
    ULONG state = page->state;
    if (state == 7)
    {
        trim(page);
    }
    else
    {
        page->state++;
    }
}
#endif

PPFN get_free_page(VOID) {
    // LM Fix: Find a way to pick a better candidate

    PPFN free_page;
    while (TRUE) {

        if (free_page_list.num_pages == 0) {
            if (standby_page_list.num_pages == 0) {
                find_trim_candidates();
            }
        }
        if (free_page_list.num_pages != 0) {
            free_page = (PPFN) RemoveHeadList(&free_page_list.entry);
            free_page_list.num_pages--;
        } else if (standby_page_list.num_pages != 0) {
            free_page = (PPFN) RemoveHeadList(&standby_page_list.entry);
            PPTE pte = free_page->pte;

            // Make this zero so a subsequent fault on this VA will know to get this from disc
            pte->software_format.frame_number = PAGE_ON_DISC;

            standby_page_list.num_pages--;
        } else {
            continue;
        }
        break;
    }
        free_page->state = ACTIVE;
        return free_page;
}

PPFN read_page_on_disc(PPTE pte)
{
    PPFN free_page = get_free_page();

    if (MapUserPhysicalPages (modified_read_va, 1, &free_page->frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %lX\n", modified_read_va, free_page->frame_number);
        return NULL;
    }

    // This would be a disc driver that does this read in a real operating system
    PVOID source = (PVOID)  ((ULONG_PTR) disc_space + pte->software_format.disc_index * PAGE_SIZE);
    memcpy(modified_read_va, source, PAGE_SIZE);

    if (MapUserPhysicalPages (modified_read_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %lX\n", modified_read_va, free_page->frame_number);
        return NULL;
    }

    // Set the char at disc_index in disc in use to be 0 to avoid leaking disc space and cause a clog up of the machine
    PUCHAR disc_spot = disc_in_use + pte->software_format.disc_index;
    *disc_spot = 0;
    return free_page;
}

VOID write_modified_pages()
{
    // LM fix implement a disc free space count global and use it to avoid iteration while checking
    while (modified_page_list.num_pages != 0)
    {
        PPFN pfn = (PPFN) RemoveHeadList(&modified_page_list.entry);
        modified_page_list.num_pages--;

        if (MapUserPhysicalPages (modified_write_va, 1, &pfn->frame_number) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %lX\n", modified_write_va, pfn->frame_number);
            return;
        }

        // Problem 2: Do not give out more virtual addresses than we have memory + disc space
        // One less = place to juggle with
        // Problem 3: Undisclosed bug


        // LM Fix what if all spots are filled
        PUCHAR disc_spot = disc_in_use;
        while (disc_spot != disc_end)
        {
            if (*disc_spot == 0)
            {
                break;
            }
            disc_spot++;
        }

        if (disc_spot == disc_end)
        {
            if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

                printf ("full_virtual_memory_test : could not unmap VA %p to page %lX\n", modified_write_va, pfn->frame_number);
                return;
            }

            InsertHeadList(&modified_page_list.entry, &pfn->entry);
            modified_page_list.num_pages++;

            return;
        }

        PVOID actual_space;
        // LM Fix edit software format in the pte to have a field for disc address
        ULONG disc_index = disc_spot - disc_in_use;
        actual_space = (PVOID) ((ULONG_PTR) disc_space + (disc_index * PAGE_SIZE));

        memcpy(actual_space, modified_write_va, PAGE_SIZE);
        *disc_spot = 1;

        pfn->pte->software_format.disc_index = disc_index;

        if (MapUserPhysicalPages (modified_write_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p to page %lX\n", modified_write_va, pfn->frame_number);
            return;
        }
        pfn->state = CLEAN;

        InsertTailList(&standby_page_list.entry, &pfn->entry);
        standby_page_list.num_pages++;
    }
}

VOID remove_from_list(PPFN pfn)
{
    // Removes the pfn from whatever list its on, does not matter which list it is
    PLIST_ENTRY prev_entry = pfn->entry.Blink;
    PLIST_ENTRY next_entry = pfn->entry.Flink;

    prev_entry->Flink = next_entry;
    next_entry->Blink = prev_entry;

    if (pfn->state == MODIFIED)
    {
        modified_page_list.num_pages--;
    }
    else
    {
        PUCHAR disc_spot = disc_in_use + pfn->pte->software_format.disc_index;
        *disc_spot = 0;
        standby_page_list.num_pages--;
    }
    pfn->state = ACTIVE;
}



BOOLEAN page_fault_handler(PVOID arbitrary_va)
{
    // Connect the virtual address now - if that succeeds then
    // we'll be able to access it from now on.

    // we need to get the pte from arbitrary va f(va)->pte
    PPTE pte = pte_from_va(arbitrary_va);
    if (pte == NULL)
    {
        printf("ERROR PTE NULL");
        return FALSE;
    }
    PTE pte_contents = *pte;

    PPFN pfn;

    // Get a free page if there is no address in the pte
    if (pte_contents.software_format.frame_number == 0) {
    // This virtual address has never been accessed
        pfn = get_free_page();
    } else if (pte_contents.software_format.frame_number == PAGE_ON_DISC) {
    // This virtual address has been previously accessed and its contents now exclusively exists on disc
    // It has been trimmed, written to disc, and its physical page has been reused.
    // Page on disc gets wiped out when we rewrite the frame number below
        pfn = read_page_on_disc(pte);
    } else {
    // This is an address that has been accessed and trimmed
    // This will unlink it from the standby or modified list
        pfn = pfn_from_frame_number(pte_contents.software_format.frame_number);
        remove_from_list(pfn);
    }
    if (pfn == NULL) {
        printf("Fatal Error: could not get a page in page fault handler");
        return FALSE;
    }

    pte->hardware_format.frame_number = pfn->frame_number;
    pte->hardware_format.valid = 1;
    pfn->pte = pte;


    if (MapUserPhysicalPages(arbitrary_va, 1, &pfn->frame_number) == FALSE) {

        printf("full_virtual_memory_test : could not map VA %p to page %lX\n", arbitrary_va, pfn->frame_number);
        return FALSE;
    }

    /* No exception handler needed now since we have connected
    the virtual address above to one of our physical pages
    so no subsequent fault can occur. */

    return TRUE;
}

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
        printf("system not initialized");
        return 1;
    }

    if (full_virtual_memory_test() == FALSE)
    {
        // LM Fix print
        return 1;
    }
    printf("full_virtual_memory_test : finished accessing random virtual addresses\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "conversions.h"

// sxd av to ignore all page faults when trying to write to arbitrary va
// Second chance is an actual exception separate from a page fault

#pragma comment(lib, "advapi32.lib")

#define PAGE_SIZE                   4096

#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two, so we can use masking to stay
// within bounds.
//

#define VIRTUAL_ADDRESS_SIZE        MB(16)

// This value is carefully picked as it fits into our field for frame number of our invalid pte format
#define PAGE_ON_DISC (ULONG_PTR) 0xFFFFFFFFFF

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

#define FREE 0
#define CLEAN 1
//#define ZEROED 2
#define MODIFIED 3
#define ACTIVE 4


BOOL GetPrivilege (VOID);
VOID create_page_file(ULONG bytes);
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
VOID full_virtual_memory_test (VOID);


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

BOOL GetPrivilege (VOID)
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege. 
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %lu\n", GetLastError ());
        return FALSE;
    } 

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    return TRUE;
}

VOID create_page_file(ULONG bytes)
{
    disc_space = malloc(bytes);
    if (disc_space == NULL)
    {
        // LM FIX
        return;
    }

    disc_in_use = malloc(bytes / PAGE_SIZE);
    if (disc_in_use == NULL)
    {
        return;
    }
    memset(disc_in_use, 0, bytes / PAGE_SIZE);

    disc_end = disc_in_use + bytes / PAGE_SIZE;
}

PPTE pte_from_va(PVOID virtual_address)
{
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
    for (ULONG_PTR i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++)
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

    return free_page;
}

VOID write_modified_pages()
{
    while (modified_page_list.num_pages != 0)
    {
        PPFN pfn = (PPFN) RemoveHeadList(&modified_page_list.entry);
        modified_page_list.num_pages--;

        if (MapUserPhysicalPages (modified_write_va, 1, &pfn->frame_number) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %lX\n", modified_write_va, pfn->frame_number);
            return;
        }

        // Problem 1: What if there are no more free disc spots?
        // Problem 2: Do not give out more virtual addresses than we have memory + disc space
        // One less = place to juggle with
        // Problem 3: Undisclosed bug

        if (disc_in_use)
        {

        }
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
        standby_page_list.num_pages--;
    }
    pfn->state = ACTIVE;
}

VOID full_virtual_memory_test (VOID)
{
    unsigned i;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    // BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_pages;

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }    

    physical_page_handle = GetCurrentProcess ();

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = (PULONG_PTR) malloc (physical_page_count * sizeof (ULONG_PTR));
    memset(physical_page_numbers, 0, physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf ("full_virtual_memory_test : allocated only %lu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }

    // Insert array of pages into free list
    InitializeListHead(&free_page_list.entry);
    pfn_metadata = malloc(physical_page_count * sizeof(PFN));
    if (pfn_metadata == NULL)
    {
        // LM Fix
        printf("full_virtual_memory_test : could not allocate memory required for pfn metadata");
    }
    memset(pfn_metadata, 0, physical_page_count * sizeof(PFN)); 

    PPFN free_pfn = pfn_metadata;
    for (i = 0; i < physical_page_count; i++)
    {

        // We are not using page zero so a frame number of zero can correspond to a page being on disc
        if (physical_page_numbers[i] == PAGE_ON_DISC || physical_page_numbers[i] == 0)
        {
            continue;
        }

        free_pfn->frame_number = physical_page_numbers[i];
        free_pfn->state = FREE;
        InsertTailList(&free_page_list.entry, &free_pfn->entry);
        free_pfn++;
        free_page_list.num_pages++;
    }

    free(physical_page_numbers);

    InitializeListHead(&modified_page_list.entry);
    modified_page_list.num_pages = 0;

    InitializeListHead(&standby_page_list.entry);
    standby_page_list.num_pages = 0;

    // From now on, we only use the free page list instead of a page number array

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~(PAGE_SIZE - 1);

    virtual_address_size_in_pages =
                        virtual_address_size / PAGE_SIZE;

    va_base = VirtualAlloc (NULL,
                                virtual_address_size,
                                MEM_RESERVE | MEM_PHYSICAL,
                                PAGE_READWRITE);

    if (va_base == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory\n");
        return;
    }

    // LM Fix we will also need a modified read va
    modified_write_va = VirtualAlloc (NULL,
                                      PAGE_SIZE,
                                      MEM_RESERVE | MEM_PHYSICAL,
                                      PAGE_READWRITE);

    if (modified_write_va == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for modified write va\n");
        return;
    }

    modified_read_va = VirtualAlloc (NULL,
                                      PAGE_SIZE,
                                      MEM_RESERVE | MEM_PHYSICAL,
                                      PAGE_READWRITE);

    if (modified_read_va == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for modified write va\n");
        return;
    }

    PULONG_PTR p = (PULONG_PTR) va_base;

    ULONG_PTR num_pte_bytes = virtual_address_size / PAGE_SIZE * sizeof(PTE);
    pte_base = malloc(num_pte_bytes);
    pte_end = pte_base + num_pte_bytes / sizeof(PTE);
    if (pte_base == NULL)
    {
        printf("ERROR");
        return;
    }
    memset(pte_base, 0, num_pte_bytes);

    // LM Fix adjust size
    create_page_file(MB (1));
    //
    // Now perform random accesses.
    //

    for (i = 0; i < MB (1); i++) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        // randex
        random_number = rand();

        random_number %= virtual_address_size_in_pages;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        arbitrary_va = p + (random_number * PAGE_SIZE) / sizeof(ULONG_PTR);

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }


        if (page_faulted) {

            //
            // Connect the virtual address now - if that succeeds then
            // we'll be able to access it from now on.
            //
            // THIS IS JUST REUSING THE SAME PHYSICAL PAGE OVER AND OVER !
            //
            // IT NEEDS TO BE REPLACED WITH A TRUE MEMORY MANAGEMENT
            // STATE MACHINE !
            //

            // we need to get the pte from arbitrary va f(va)->pte
            PPTE pte = pte_from_va(arbitrary_va);
            PTE pte_contents = *pte;

        // LM FIX CHECK FOR THE VALID BIT IN THE FUTURE and initialize the pfn

            PPFN pfn;

            // Get a free page if there is no address in the pte

            if (pte_contents.software_format.frame_number == 0)
            {
                // This virtual address has never been accessed
                pfn = get_free_page();
            }
            else if (pte_contents.software_format.frame_number == PAGE_ON_DISC)
            {
                // This virtual address has been previously accessed and its contents now exclusively exists on disc
                // It has been trimmed, written to disc, and its physical page has been reused.
                pfn = read_page_on_disc(pte);
            }
            else
            {
                // This is an address that has been accessed and trimmed
                // This will unlink it from the standby or modified list
                pfn = pfn_from_frame_number(pte_contents.software_format.frame_number);
                remove_from_list(pfn);
            }
            if (pfn == NULL)
            {
                printf("Fatal Error: could not get a page in page fault handler");
                return;
            }

            pte->hardware_format.frame_number = pfn->frame_number;
            pte->hardware_format.valid = 1;
            pfn->pte = pte;

            // LM Fix

            if (MapUserPhysicalPages (arbitrary_va, 1, &pfn->frame_number) == FALSE) {

                printf ("full_virtual_memory_test : could not map VA %p to page %lX\n", arbitrary_va, *physical_page_numbers);

                return;
            }

            /* No exception handler needed now since we have connected
            the virtual address above to one of our physical pages
            so no subsequent fault can occur. */

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            // Unmap the virtual address translation we installed above now that we're done writing our value into it.
            #if 0
            if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {

                printf ("full_virtual_memory_test : could not unmap VA %p\n", arbitrary_va);

                return;
            }
            #endif

        }
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    // Now that we're done with our memory we can be a good citizen and free it.
    VirtualFree (p, 0, MEM_RELEASE);
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
    full_virtual_memory_test ();

    return 1;
}

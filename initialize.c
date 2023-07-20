#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "conversions.h"
#include "system.h"

#pragma comment(lib, "advapi32.lib")

// LM Fix scale this to the system (for example 10% of system memory)
#define NUMBER_OF_PHYSICAL_PAGES                 (MB (1) / PAGE_SIZE)
#define NUMBER_OF_DISC_PAGES                     (MB (16) / PAGE_SIZE)


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

ULONG_PTR create_page_file(ULONG_PTR bytes)
{
    // LM Fix be more sophisticated by scaling disc size down while malloc fails
    // Reduce bytes in the loop
    disc_space = malloc(bytes);
    if (disc_space == NULL)
    {
        return 0;
    }

    // LM Fix merge these possibly
    ULONG_PTR size = bytes / PAGE_SIZE;
    disc_in_use = malloc(size);
    if (disc_in_use == NULL)
    {
        return 0;
    }
    memset(disc_in_use, 0, size);

    disc_end = disc_in_use + size;
    return bytes;
}

// LM Fix separate into other functions
BOOLEAN initialize_system (VOID) {
    unsigned i;
    BOOL allocated;
    BOOL privilege;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;


    // Allocate the physical pages that we will be managing.
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.

    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf("full_virtual_memory_test : could not get privilege\n");
        return FALSE;
    }

    physical_page_handle = GetCurrentProcess();

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    // Store all the frame numbers of every page we take
    physical_page_numbers = (PULONG_PTR) malloc(physical_page_count * sizeof(ULONG_PTR));
    // No memset needed as we will initialize it immediately

    if (physical_page_numbers == NULL) {
        printf("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return FALSE;
    }

    allocated = AllocateUserPhysicalPages(physical_page_handle,
                                          &physical_page_count,
                                          physical_page_numbers);

    if (allocated == FALSE) {
        printf("full_virtual_memory_test : could not allocate physical pages\n");
        return FALSE;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("full_virtual_memory_test : allocated only %lu pages out of %u pages requested\n",
                // LM Fix va space will be restricted if this condition is met, we need to make it a global and compute it after this
               physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }

    // Insert array of pages into free list
    InitializeListHead(&free_page_list.entry);
    pfn_metadata = malloc(physical_page_count * sizeof(PFN));
    if (pfn_metadata == NULL) {
        // LM Fix
        printf("full_virtual_memory_test : could not allocate memory required for pfn metadata");
    }
    memset(pfn_metadata, 0, physical_page_count * sizeof(PFN));

    // LM Fix initialize list heads in separate function, have another for populating free list
    PPFN free_pfn = pfn_metadata;
    for (i = 0; i < physical_page_count; i++) {

        // We are not using page zero so a frame number of zero can correspond to a page being on disc
        if (physical_page_numbers[i] == PAGE_ON_DISC || physical_page_numbers[i] == 0) {
            continue;
        }

        free_pfn->frame_number = physical_page_numbers[i];
        free_pfn->state = FREE;
        InsertTailList(&free_page_list.entry, &free_pfn->entry);
        free_pfn++;
        free_page_list.num_pages++;
    }

    // From now on, we only use the free page list instead of a page number array
    free(physical_page_numbers);

    InitializeListHead(&modified_page_list.entry);
    modified_page_list.num_pages = 0;

    InitializeListHead(&standby_page_list.entry);
    standby_page_list.num_pages = 0;

    disc_page_count = create_page_file(NUMBER_OF_DISC_PAGES * PAGE_SIZE) / PAGE_SIZE;

    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    // We want more virtual than physical memory to illustrate this illusion
    // We also do not give too much virtual memory as we still want to be able to illustrate the illusion

    virtual_address_size = (physical_page_count + disc_page_count - 1) * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary.
    virtual_address_size &= ~(PAGE_SIZE - 1);

    va_base = VirtualAlloc(NULL,
                           virtual_address_size,
                           MEM_RESERVE | MEM_PHYSICAL,
                           PAGE_READWRITE);

    if (va_base == NULL) {
        printf("full_virtual_memory_test : could not reserve memory\n");
        return FALSE;
    }

    // Put into a function
    modified_write_va = VirtualAlloc(NULL,
                                     PAGE_SIZE,
                                     MEM_RESERVE | MEM_PHYSICAL,
                                     PAGE_READWRITE);

    if (modified_write_va == NULL) {
        printf("full_virtual_memory_test : could not reserve memory for modified write va\n");
        return FALSE;
    }

    modified_read_va = VirtualAlloc(NULL,
                                    PAGE_SIZE,
                                    MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);

    if (modified_read_va == NULL) {
        printf("full_virtual_memory_test : could not reserve memory for modified write va\n");
        return FALSE;
    }

    ULONG_PTR num_pte_bytes = virtual_address_size / PAGE_SIZE * sizeof(PTE);
    pte_base = malloc(num_pte_bytes);
    pte_end = pte_base + num_pte_bytes / sizeof(PTE);
    if (pte_base == NULL) {
        printf("ERROR");
        return FALSE;
    }
    memset(pte_base, 0, num_pte_bytes);

    return TRUE;
}
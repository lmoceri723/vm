#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "structs.h"
#include "system.h"

#pragma comment(lib, "advapi32.lib")

#define NUMBER_OF_PHYSICAL_PAGES                 (MB (1) / PAGE_SIZE)
#define NUMBER_OF_DISC_PAGES                     (MB (16) / PAGE_SIZE)

int compare (const void * a, const void * b);

unsigned i;
PULONG_PTR physical_page_numbers;
HANDLE physical_page_handle;

BOOL GetPrivilege(VOID)
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    // Open the token.
    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,TOKEN_ADJUST_PRIVILEGES,&Token);

    if (Result == FALSE) {
        printf ("get_privilege : cannot open process token.\n");
        return FALSE;
    }

    // Enable the privilege.
    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the LUID.
    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("get_privilege : cannot get privilege\n");
        return FALSE;
    }

    // Adjust the privilege and check the result
    Result = AdjustTokenPrivileges (Token,FALSE,(PTOKEN_PRIVILEGES) &Info,
                                    0,NULL,NULL);

    if (Result == FALSE) {
        printf ("get_privilege : cannot adjust token privileges %lu\n", GetLastError ());
        return FALSE;
    }

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("get_privilege : cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle(Token);

    return TRUE;
}

BOOLEAN create_page_file(ULONG_PTR bytes)
{
    // When we search for free bits, we use MAXULONG32 to say there is no free bit
    // So our size cannot be greater than it
    if (bytes >= MAXULONG32 * PAGE_SIZE)
    {
        printf("create_page_file : amount of bytes in page file exceed its maximum possible size");
        return FALSE;
    }
    // LM Fix be more sophisticated by scaling disc size down while malloc fails by reducing bytes in the loop
    ULONG_PTR actual_bytes = bytes;
    disc_space = malloc(actual_bytes);
    // LM Fix make this a function and apply this to all cases of mallocing in this file
#if 0
    while (disc_space == NULL)
    {
        actual_bytes = actual_bytes * 0.9;
        disc_space = malloc(actual_bytes);
    }
    if (actual_bytes != bytes)
    {
        printf("create_page_file : could only allocate %lu out of %lu bytes for the disc space", actual_bytes, bytes);
    }
#endif

    if (disc_space == NULL)
    {
        printf("create_page_file : could not allocate memory for disc space");
        return FALSE;
    }

    // LM Fix merge mallocs to be one single malloc
    ULONG_PTR size = bytes / PAGE_SIZE / 8;
    disc_in_use = malloc(size);
    if (disc_in_use == NULL)
    {
        printf("create_page_file : could not allocate memory for disc in use array");
        return FALSE;
    }
    memset(disc_in_use, 0, size);

    disc_end = disc_in_use + size;

    disc_page_count = bytes;

    return TRUE;
}

ULONG_PTR dynamic_malloc(PVOID variable, ULONG_PTR bytes)
{
    ULONG_PTR actual_bytes = bytes;
    variable = malloc(actual_bytes);

    while (variable == NULL)
    {
        actual_bytes = actual_bytes * 0.9;
        variable = malloc(actual_bytes);
    }
    if (actual_bytes != bytes)
    {
        printf("dynamic_malloc : could only allocate %lu out of %lu bytes for the variable", actual_bytes, bytes);
    }

    return actual_bytes;
}


VOID initialize_page_lists(VOID)
{
    InitializeListHead(&free_page_list.entry);
    free_page_list.num_pages = 0;

    InitializeListHead(&modified_page_list.entry);
    modified_page_list.num_pages = 0;

    InitializeListHead(&standby_page_list.entry);
    standby_page_list.num_pages = 0;

    for (i = 0; i < 8; i++)
    {
        InitializeListHead(&active_page_list[i].entry);
        active_page_list[i].num_pages = 0;
    }
}

// LM Fix combine this with initialize pfn metadata
VOID populate_free_list(VOID)
{
    PPFN free_pfn;
    ULONG_PTR frame_number;

    for (i = 0; i < physical_page_count; i++) {

        frame_number = physical_page_numbers[i];

        // We are not using page zero so a frame number of zero can correspond to a page being on disc
        if (frame_number == PAGE_ON_DISC || frame_number == 0) {
            continue;
        }

        free_pfn = pfn_from_frame_number(frame_number);

        free_pfn->flags.state = FREE;
        InsertTailList(&free_page_list.entry, &free_pfn->entry);
        free_page_list.num_pages++;
    }
    // From now on, we only use the free page list instead of a page number array
     free(physical_page_numbers);
}

BOOLEAN initialize_readwrite_va(VOID)
{
    modified_write_va = VirtualAlloc(NULL,
                                     PAGE_SIZE,
                                     MEM_RESERVE | MEM_PHYSICAL,
                                     PAGE_READWRITE);

    if (modified_write_va == NULL) {
        printf("initialize_readwrite_va : could not reserve memory for modified write va\n");
        return FALSE;
    }

    modified_read_va = VirtualAlloc(NULL,
                                    PAGE_SIZE,
                                    MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);

    if (modified_read_va == NULL) {
        printf("initialize_readwrite_va : could not reserve memory for modified write va\n");
        return FALSE;
    }

    return TRUE;
}

BOOLEAN initialize_va_space(VOID)
{
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
        printf("initialize_va_space : could not reserve memory for virtual addresses\n");
        return FALSE;
    }

    return TRUE;
}

BOOLEAN initialize_pte_metadata(VOID)
{
    ULONG_PTR num_pte_bytes = virtual_address_size / PAGE_SIZE * sizeof(PTE);
    pte_base = malloc(num_pte_bytes);
    pte_end = pte_base + num_pte_bytes / sizeof(PTE);
    if (pte_base == NULL) {
        printf("initialize_pte_metadata : could not reserve memory for pte metadata\n");
        return FALSE;
    }
    memset(pte_base, 0, num_pte_bytes);

    return TRUE;
}

BOOLEAN initialize_pfn_metadata(VOID)
{
    ULONG_PTR range = physical_page_numbers[physical_page_count - 1];

    pfn_metadata = VirtualAlloc(NULL,range * sizeof(PFN),
                                MEM_RESERVE,PAGE_READWRITE);

    if (pfn_metadata == NULL) {
        printf("initialize_pfn_metadata : could not reserve memory for pfn metadata\n");
        return FALSE;
    }

    for (i = 0; i < physical_page_count; i++)
    {
        if (physical_page_numbers[i] == PAGE_ON_DISC || physical_page_numbers[i] == 0) {
            continue;
        }

        LPVOID result = VirtualAlloc(pfn_metadata + physical_page_numbers[i],sizeof(PFN),
                     MEM_COMMIT,PAGE_READWRITE);

        if (result == NULL) {
            printf("initialize_pfn_metadata : could not commit memory for pfn %u in pfn metadata\n", i);
            return FALSE;
        }

        memset(pfn_metadata + physical_page_numbers[i], 0, sizeof(PFN));
    }
    return TRUE;
}

BOOLEAN initialize_pages()
{
    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    // Store all the frame numbers of every page we take
    physical_page_numbers = (PULONG_PTR) malloc(physical_page_count * sizeof(ULONG_PTR));
    // No memset needed as we will initialize it immediately

    if (physical_page_numbers == NULL) {
        printf("initialize_pages : could not allocate array to hold physical page numbers\n");
        return FALSE;
    }

    if (AllocateUserPhysicalPages(physical_page_handle,&physical_page_count,
                                  physical_page_numbers) == FALSE) {
        printf("initialize_pages : could not allocate physical pages\n");
        return FALSE;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("initialize_pages : allocated only %lu pages out of %u pages requested\n",
                // LM Fix va space will be restricted if this condition is met, we need to make it a global and compute it after this
               physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }


    qsort(physical_page_numbers, physical_page_count, sizeof(ULONG_PTR), compare);
    return TRUE;
}

int compare (const void *a, const void *b) {
    return ( *(PULONG_PTR)a - *(PULONG_PTR)b );
}

BOOLEAN initialize_system (VOID) {

    // First acquire privilege as the operating system reserves the sole right to allocate pages.
    if (GetPrivilege() == FALSE) {
        printf("initialize_system : could not get privilege\n");
        return FALSE;
    }

    physical_page_handle = GetCurrentProcess();

    if (initialize_pages() == FALSE)
    {
        printf("initialize_system : could not initialize pages\n");
        return FALSE;
    }

    // Insert array of pages into free list
    if (initialize_pfn_metadata() == FALSE)
    {
        printf("initialize_system : could not initialize pfn metadata\n");
        return FALSE;
    }

    initialize_page_lists();
    populate_free_list();

    if (create_page_file(NUMBER_OF_DISC_PAGES * PAGE_SIZE) == FALSE)
    {
        printf("initialize_system : could not create page file\n");
        return FALSE;
    }

    if (initialize_va_space() == FALSE)
    {
        printf("initialize_system : could not initialize va space\n");
        return FALSE;
    }

    if (initialize_readwrite_va() == FALSE)
    {
        printf("initialize_system : could not initialize readwrite vas\n");
        return FALSE;
    }

    if (initialize_pte_metadata() == FALSE)
    {
        printf("initialize_system : could not initialize pte metadata\n");
        return FALSE;
    }

    return TRUE;
}

#if 0
// LM Fix make this free everything
// Only what we have malloced or virtualalloced
// Do corresponding things to each (malloc = free, virtualalloc = virtualfree)
BOOLEAN deinitialize_system (VOID)
{
    // Now that we're done with our memory we can be a good citizen and free it.
    VirtualFree(p, 0, MEM_RELEASE);

    // ONLY FREE WHAT I HAVE MALLOCED
    free((PVOID) physical_page_count);
    free((PVOID) disc_page_count);
    free((PVOID) virtual_address_size);
    free(pte_base);
    free(pte_end);
    free(va_base);
    free(modified_read_va);
    free(modified_write_va);
    free(disc_space);
    free(disc_in_use);
    free(disc_end);
    free(pfn_metadata);
    free((PVOID) i);
    free(physical_page_handle);
}
#endif
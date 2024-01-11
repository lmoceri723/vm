#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "../include/macros.h"
#include "../include/structs.h"
#include "../include/system.h"

#pragma comment(lib, "advapi32.lib")

int compare(const void * a, const void * b);

PULONG_PTR physical_page_numbers;

// These are required to be initialized by the thread api, but they are not used currently

PHANDLE thread_handles;
PULONG thread_ids;

HANDLE system_handles[NUMBER_OF_SYSTEM_THREADS];
ULONG system_thread_ids[NUMBER_OF_SYSTEM_THREADS];
HANDLE physical_page_handle;

HANDLE wake_aging_event;
HANDLE modified_writing_event;
HANDLE pages_available_event;
HANDLE disc_spot_available_event;
HANDLE system_exit_event;

CRITICAL_SECTION pte_lock;
CRITICAL_SECTION pfn_lock;
CRITICAL_SECTION pte_region_locks[NUMBER_OF_PTE_REGIONS];
CRITICAL_SECTION disc_in_use_lock;

SYSTEM_INFO info;
ULONG num_processors;


VOID GetPrivilege(VOID)
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
        fatal_error();
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
        fatal_error();
    }

    // Adjust the privilege and check the result
    Result = AdjustTokenPrivileges (Token,FALSE,(PTOKEN_PRIVILEGES) &Info,
                                    0,NULL,NULL);

    if (Result == FALSE) {
        printf ("get_privilege : cannot adjust token privileges %lu\n", GetLastError ());
        fatal_error();
    }

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("get_privilege : cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
         fatal_error();
    }

    CloseHandle(Token);
}

VOID initialize_locks(VOID)
{
    InitializeCriticalSection(&pte_lock);
    InitializeCriticalSection(&pfn_lock);
    InitializeCriticalSection(&disc_in_use_lock);

    InitializeCriticalSection(&free_page_list.lock);
    InitializeCriticalSection(&standby_page_list.lock);
    InitializeCriticalSection(&modified_page_list.lock);

    for (unsigned i = 0; i < NUMBER_OF_PTE_REGIONS; i++)
    {
        InitializeCriticalSection(&pte_region_locks[i]);
    }
}

VOID initialize_events(VOID)
{
    wake_aging_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (wake_aging_event == NULL)
    {
        printf("initialize_events : could not initialize wake_aging_event");
        fatal_error();
    }

    modified_writing_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (modified_writing_event == NULL)
    {
        printf("initialize_events : could not initialize modified_writing_event");
        fatal_error();
    }

    pages_available_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (pages_available_event == NULL)
    {
        printf("initialize_events : could not initialize pages_available_event");
        fatal_error();
    }

    disc_spot_available_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (disc_spot_available_event == NULL)
    {
        printf("initialize_events : could not initialize disc_spot_available_event");
        fatal_error();
    }

    system_exit_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (system_exit_event == NULL)
    {
        printf("initialize_events : could not initialize pages_available_event");
        fatal_error();
    }
}

VOID initialize_threads(VOID)
{
    GetSystemInfo(&info);
    num_processors = info.dwNumberOfProcessors;

    thread_handles = malloc(sizeof(HANDLE) * num_processors);
    thread_ids = malloc(sizeof(ULONG) * num_processors);

    system_handles[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) trim_thread,
                 (LPVOID) (ULONG_PTR) 0, 0, &system_thread_ids[0]);
    if (system_handles[0] == NULL)
    {
        printf("initialize_threads : could not initialize thread handle for trim_thread");
        fatal_error();
    }

    system_handles[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) modified_write_thread,
                 (LPVOID) (ULONG_PTR) 1, 0, &system_thread_ids[1]);
    if (system_handles[1] == NULL)
    {
        printf("initialize_threads : could not initialize thread handle for modified_write_thread");
        fatal_error();
    }
}

// LM FUTURE FIX make this an actual disc write
VOID initialize_page_file(ULONG64 num_disc_pages)
{
    ULONG64 bytes;
    ULONG64 bitmap_size_in_bytes;

    // We have 101% assurance of this
    bytes = num_disc_pages * PAGE_SIZE;

    disc_space = malloc(bytes);
    if (disc_space == NULL)
    {
        printf("initialize_page_file : could not allocate memory for disc space");
        fatal_error();
    }
    //memset(disc_space, 0, bytes);

    // We are 100% sure of this
    bitmap_size_in_bytes = num_disc_pages / BITS_PER_BYTE;

    disc_in_use = malloc(bitmap_size_in_bytes);
    if (disc_in_use == NULL)
    {
        printf("initialize_page_file : could not allocate memory for disc in use array");
        fatal_error();
    }
    memset(disc_in_use, 0, bitmap_size_in_bytes);

    disc_in_use_end = disc_in_use + bitmap_size_in_bytes;
    disc_page_count = num_disc_pages;
}


VOID initialize_page_lists(VOID)
{
    InitializeListHead(&free_page_list.entry);
    free_page_list.num_pages = 0;

    InitializeListHead(&modified_page_list.entry);
    modified_page_list.num_pages = 0;

    InitializeListHead(&standby_page_list.entry);
    standby_page_list.num_pages = 0;

}

VOID initialize_readwrite_va(VOID)
{
    modified_write_va = VirtualAlloc(NULL,
                                     PAGE_SIZE,
                                     MEM_RESERVE | MEM_PHYSICAL,
                                     PAGE_READWRITE);

    if (modified_write_va == NULL) {
        printf("initialize_readwrite_va : could not reserve memory for modified write va\n");
        fatal_error();
    }

    modified_read_va = VirtualAlloc(NULL,
                                    PAGE_SIZE,
                                    MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);

    if (modified_read_va == NULL) {
        printf("initialize_readwrite_va : could not reserve memory for modified write va\n");
        fatal_error();
    }

    repurpose_zero_va = VirtualAlloc(NULL,
                                    PAGE_SIZE,
                                    MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);

    if (repurpose_zero_va == NULL) {
        printf("initialize_readwrite_va : could not reserve memory for repurpose zero va\n");
        fatal_error();
    }
}

VOID initialize_va_space(VOID)
{
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    // We want more virtual than physical memory to illustrate this illusion.
    // We also do not give too much virtual memory as we still want to be able to illustrate the illusion

    // LM ASK why -1 here?
    virtual_address_size = (physical_page_count + disc_page_count - 1) * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary
    // Uses bit operations instead of modulus to do this quicker
    virtual_address_size &= ~(PAGE_SIZE - 1);

    va_base = VirtualAlloc(NULL,
                           virtual_address_size,
                           MEM_RESERVE | MEM_PHYSICAL,
                           PAGE_READWRITE);

    if (va_base == NULL) {
        printf("initialize_va_space : could not reserve memory for virtual addresses\n");
        fatal_error();
    }
}

VOID initialize_pte_metadata(VOID)
{
    ULONG_PTR num_pte_bytes = virtual_address_size / PAGE_SIZE * sizeof(PTE);
    pte_base = malloc(num_pte_bytes);
    if (pte_base == NULL) {
        printf("initialize_pte_metadata : could not reserve memory for pte metadata\n");
        fatal_error();
    }
    memset(pte_base, 0, num_pte_bytes);
    pte_end = pte_base + num_pte_bytes / sizeof(PTE);

}

VOID initialize_pfn_metadata(VOID)
{
   highest_frame_number = physical_page_numbers[physical_page_count - 1];

    pfn_base = VirtualAlloc(NULL, highest_frame_number * sizeof(PFN),
                            MEM_RESERVE, PAGE_READWRITE);
    pfn_end = pfn_base + highest_frame_number * sizeof(PFN);

    if (pfn_base == NULL) {
        printf("initialize_pfn_metadata : could not reserve memory for pfn metadata\n");
        fatal_error();
    }

    PPFN pfn;
    ULONG_PTR frame_number;

    for (ULONG64 i = 0; i < physical_page_count; i++)
    {
        frame_number = physical_page_numbers[i];
        // Frame number of 0 is used to signify that a pte has not yet been connected to a page and is brand new
        if (frame_number == 0) {
            continue;
        }

        LPVOID result = VirtualAlloc(pfn_base + physical_page_numbers[i], sizeof(PFN),
                                     MEM_COMMIT, PAGE_READWRITE);

        if (result == NULL) {
            printf("initialize_pfn_metadata : could not commit memory for pfn %llu in pfn metadata\n", i);
            fatal_error();
        }

        memset(pfn_base + physical_page_numbers[i], 0, sizeof(PFN));

        pfn = pfn_from_frame_number(frame_number);
        pfn->flags.state = FREE;
        InitializeCriticalSection(&pfn->flags.lock);

        InsertTailList(&free_page_list.entry, &pfn->entry);
        free_page_list.num_pages++;
    }

    //free(physical_page_numbers);
}

VOID initialize_pages()
{
    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    // Store all the frame numbers of every page we take
    physical_page_numbers = (PULONG_PTR) malloc(physical_page_count * sizeof(ULONG_PTR));
    // No memset needed as we will initialize it immediately

    if (physical_page_numbers == NULL) {
        printf("initialize_pages : could not allocate array to hold physical page numbers\n");
        fatal_error();
    }

    if (AllocateUserPhysicalPages(physical_page_handle,&physical_page_count,
                                  physical_page_numbers) == FALSE) {
        printf("initialize_pages : could not allocate physical pages\n");
        fatal_error();
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("initialize_pages : allocated only %llu pages out of %u pages requested\n",
               physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }


    qsort(physical_page_numbers, physical_page_count, sizeof(ULONG_PTR), compare);
}

int compare (const void *a, const void *b) {
    return ( *(PULONG_PTR) a - *(PULONG_PTR) b );
}

VOID initialize_system (VOID) {

    // First, acquire privilege as the operating system reserves the sole right to allocate pages.
    GetPrivilege();

    physical_page_handle = GetCurrentProcess();

    initialize_locks();
    initialize_events();

    initialize_pages();

    initialize_page_lists();

    initialize_pfn_metadata();

    initialize_page_file(NUMBER_OF_DISC_PAGES);

    initialize_va_space();

    initialize_readwrite_va();

    initialize_pte_metadata();

    initialize_threads();

    printf("initialize_system : system successfully initialized\n");
}

// Terminates the program and gives all resources back to the operating system
VOID deinitialize_system (VOID)
{
    // We need to close all system threads and wait for them to exit before proceeding
    // This happens so that no thread tries to access a data structure that we have freed
    SetEvent(system_exit_event);
    WaitForMultipleObjects(NUMBER_OF_SYSTEM_THREADS, system_handles, TRUE, INFINITE);

    // Now that we're done with our memory, we are able to free it
    free(pte_base);
    VirtualFree(modified_read_va, PAGE_SIZE, MEM_RELEASE);
    VirtualFree(modified_write_va, PAGE_SIZE, MEM_RELEASE);
    VirtualFree(va_base, virtual_address_size, MEM_RELEASE);
    free(disc_in_use);
    free(disc_space);
    VirtualFree(pfn_base, physical_page_numbers[physical_page_count - 1] * sizeof(PFN),
                MEM_RELEASE);

    // We can also do the same for all of our pages
    FreeUserPhysicalPages(physical_page_handle,&physical_page_count,
                          physical_page_numbers);
}

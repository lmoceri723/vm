#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "../include/macros.h"
#include "../include/structs.h"
#include "../include/system.h"
#include "../include/debug.h"

#pragma comment(lib, "advapi32.lib")

int compare(const void * a, const void * b);

PULONG_PTR physical_page_numbers;

// These are handles to our faulting threads
HANDLE faulting_handles[NUMBER_OF_FAULTING_THREADS];
ULONG faulting_thread_ids[NUMBER_OF_FAULTING_THREADS];
FAULT_STATS fault_stats[NUMBER_OF_FAULTING_THREADS];

// These are handles to our system threads, our trimming/modified-writing threads
HANDLE system_handles[NUMBER_OF_SYSTEM_THREADS];
ULONG system_thread_ids[NUMBER_OF_SYSTEM_THREADS];

HANDLE physical_page_handle;

// These are handles to our events, which are used to signal between threads
HANDLE wake_aging_event;
HANDLE modified_writing_event;
HANDLE pages_available_event;
HANDLE disc_spot_available_event;
HANDLE system_exit_event;
HANDLE system_start_event;

// These are the locks used in our system
CRITICAL_SECTION pte_region_locks[NUMBER_OF_PTE_REGIONS];
CRITICAL_SECTION disc_in_use_lock;
CRITICAL_SECTION modified_write_va_lock;
CRITICAL_SECTION modified_read_va_lock;
CRITICAL_SECTION repurpose_zero_va_lock;

// These are page file handles
HANDLE page_file;
HANDLE page_file_mapping;

// This is Windows-specific code to acquire a privilege.
VOID GetPrivilege(VOID)
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    // Open the token.
    hProcess = GetCurrentProcess();

    Result = OpenProcessToken (hProcess,TOKEN_ADJUST_PRIVILEGES,&Token);

    if (Result == FALSE) {
        fatal_error("get_privilege : cannot open process token.");
    }

    // Enable the privilege.
    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the LUID.
    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        fatal_error("get_privilege : cannot get privilege");
    }

    // Adjust the privilege and check the result
    Result = AdjustTokenPrivileges (Token,FALSE,(PTOKEN_PRIVILEGES) &Info,
                                    0,NULL,NULL);

    if (Result == FALSE) {
        printf("get_privilege : cannot adjust token privileges %lu", GetLastError ());
        fatal_error(NULL);
    }

    if (GetLastError () != ERROR_SUCCESS) {
        fatal_error("get_privilege : cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy");
    }

    CloseHandle(Token);
}

// This function is used to initialize all the locks used in the system
VOID initialize_locks(VOID)
{
    INITIALIZE_LOCK(disc_in_use_lock);
    INITIALIZE_LOCK(modified_write_va_lock);
    INITIALIZE_LOCK(modified_read_va_lock);
    INITIALIZE_LOCK(repurpose_zero_va_lock);

    INITIALIZE_LOCK(free_page_list.lock);
    INITIALIZE_LOCK(standby_page_list.lock);
    INITIALIZE_LOCK(modified_page_list.lock);

    for (unsigned i = 0; i < NUMBER_OF_PTE_REGIONS; i++)
    {
        INITIALIZE_LOCK(pte_region_locks[i]);
    }
}

// This function is used to initialize all the events used in the system
VOID initialize_events(VOID)
{
    // Synchronization Events
    wake_aging_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    NULL_CHECK(wake_aging_event, "initialize_events : could not initialize wake_aging_event")

    modified_writing_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    NULL_CHECK(modified_writing_event, "initialize_events : could not initialize modified_writing_event")

    // TODO LM FIX Not a synchronization event
    pages_available_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    NULL_CHECK(pages_available_event, "initialize_events : could not initialize pages_available_event")

    disc_spot_available_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    NULL_CHECK(disc_spot_available_event, "initialize_events : could not initialize disc_spot_available_event")

    // Notification Events
    system_exit_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    NULL_CHECK(system_exit_event, "initialize_events : could not initialize system_exit_event")

    system_start_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    NULL_CHECK(system_start_event, "initialize_events : could not initialize system_start_event")
}

// This function initializes all of our threads. Once they are initialized, they immediately start running.
// For this reason, this needs to be our last step of initialization
VOID initialize_threads(VOID)
{
//    SYSTEM_INFO info;
//    ULONG num_processors;

//    GetSystemInfo(&info);
//    In my system, this is 16
//    num_processors = info.dwNumberOfProcessors;

    for (ULONG i = 0; i < NUMBER_OF_FAULTING_THREADS; i++)
    {
        faulting_handles[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
        faulting_thread,(LPVOID) (ULONG_PTR) i, 0, &faulting_thread_ids[i]);
        NULL_CHECK(faulting_handles[i], "initialize_threads : could not initialize thread handle for faulting_thread")
    }

    // Initialize faulting thread stat counters
    for (ULONG i = 0; i < NUMBER_OF_FAULTING_THREADS; i++)
    {
        fault_stats[i].num_faults = 0;
        fault_stats[i].num_first_accesses = 0;
        fault_stats[i].num_reaccesses = 0;
        fault_stats[i].num_fake_faults = 0;
    }

    system_handles[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
    trim_thread,(LPVOID) (ULONG_PTR) 0, 0, &system_thread_ids[0]);
    NULL_CHECK(system_handles[0], "initialize_threads : could not initialize thread handle for trim_thread")

    system_handles[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)
    modified_write_thread,(LPVOID) (ULONG_PTR) 1, 0, &system_thread_ids[1]);
    NULL_CHECK(system_handles[1], "initialize_threads : could not initialize thread handle for modified_write_thread")
}

// This creates our page file
// Currently, we allocate a chunk of system memory to simulate a page file
// In the future, we will incorporate a real page file
VOID initialize_page_file(ULONG64 num_disc_pages)
{
    ULONG64 page_file_size_in_bytes;
    ULONG64 bitmap_size_in_bytes;

//    // Create the actual file
//    page_file = CreateFile("pagefile.sys", GENERIC_READ | GENERIC_WRITE, 0,
//                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
//                       NULL);
//    if (page_file == INVALID_HANDLE_VALUE) {
//        fatal_error("create_page_file : could not create page file");
//    }
//
//    // Set the size of the file
    page_file_size_in_bytes = num_disc_pages * PAGE_SIZE;
//    if (SetFilePointer(page_file, page_file_size_in_bytes, NULL,
//                       FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
//        fatal_error("create_page_file : could not set file pointer");
//    }
//    if (SetEndOfFile(page_file) == 0) {
//        fatal_error("create_page_file : could not set end of file");
//    }
//
//    // Create a file mapping for the page file
//    page_file_mapping = CreateFileMapping(page_file, NULL, PAGE_READWRITE,
//                                          0, 0, NULL);
//    if (page_file_mapping == NULL) {
//        printf("create_page_file : could not create file mapping %lu\n", GetLastError());
//        fatal_error(NULL);
//    }
//
//    (PVOID) MapViewOfFile(page_file_mapping, FILE_MAP_ALL_ACCESS,
//                          0, 0, 0);
    // Allocates the memory for the page file
    disc_space = malloc(page_file_size_in_bytes);
    NULL_CHECK(disc_space, "create_page_file : could not map view of page file")

    // Allocates the memory for the disc in use bitmap
    // This will remain in memory, as it is used to track which pages are in use and not part of the page file

    // We want one bit per page, so our page file is num_disc_pages bits long
    // We need this quantity in bytes to allocate memory for it, so we divide by BITS_PER_BYTE
    bitmap_size_in_bytes = num_disc_pages / BITS_PER_BYTE;

    disc_in_use = malloc(bitmap_size_in_bytes);
    NULL_CHECK(disc_in_use, "create_page_file : could not allocate memory for disc in use array")
    memset(disc_in_use, EMPTY_UNIT, bitmap_size_in_bytes);

    disc_in_use_end = disc_in_use + bitmap_size_in_bytes;
    disc_page_count = num_disc_pages;
}

// This function initializes our page lists
VOID initialize_page_lists(VOID)
{
    InitializeListHead(&free_page_list.entry);
    free_page_list.num_pages = 0;

    InitializeListHead(&modified_page_list.entry);
    modified_page_list.num_pages = 0;

    InitializeListHead(&standby_page_list.entry);
    standby_page_list.num_pages = 0;

}

// This initializes va space for our system to map pages into
// This is used when we zero out a page
// Or when we move a page from the page file to memory and vice versa
VOID initialize_system_va_space(VOID)
{
    modified_write_va = VirtualAlloc(NULL,PAGE_SIZE,MEM_RESERVE | MEM_PHYSICAL,
                                     PAGE_READWRITE);
    NULL_CHECK(modified_write_va, "initialize_system_va_space : could not reserve memory for modified write va")

    modified_read_va = VirtualAlloc(NULL,PAGE_SIZE,MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);
    NULL_CHECK(modified_read_va, "initialize_system_va_space : could not reserve memory for modified read va")

    repurpose_zero_va = VirtualAlloc(NULL,PAGE_SIZE,MEM_RESERVE | MEM_PHYSICAL,
                                    PAGE_READWRITE);
    NULL_CHECK(repurpose_zero_va, "initialize_system_va_space : could not reserve memory for repurpose zero va")
}

// This function initializes our virtual address space
VOID initialize_user_va_space(VOID)
{
    // Reserve a user address space region using the Windows kernel AWE (address windowing extensions) APIs
    // This will let us connect physical pages of our choosing to any given virtual address within our allocated region
    // We deliberately make this much larger than physical memory to illustrate how we can manage the illusion.
    // We need to still have a large enough amount of physical memory in order to have any performance

    virtual_address_size = (physical_page_count + disc_page_count - 1) * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary
    // Uses bit operations instead of modulus to do this quicker
    virtual_address_size &= ~(PAGE_SIZE - 1);

    va_base = VirtualAlloc(NULL, virtual_address_size,MEM_RESERVE | MEM_PHYSICAL,
                           PAGE_READWRITE);
    NULL_CHECK(va_base, "initialize_user_va_space : could not reserve memory for va space")
}

// This function initializes the PTEs that we use to map virtual addresses to physical pages
VOID initialize_pte_metadata(VOID)
{
    ULONG_PTR num_pte_bytes = virtual_address_size / PAGE_SIZE * sizeof(PTE);
    pte_base = malloc(num_pte_bytes);
    NULL_CHECK(pte_base, "initialize_pte_metadata : could not allocate memory for pte metadata")

    memset(pte_base, 0, num_pte_bytes);

    pte_end = pte_base + num_pte_bytes / sizeof(PTE);
}

// This function initializes the PFNs that we use to track the state of physical pages
VOID initialize_pfn_metadata(VOID)
{
    // This holds the highest frame number in our page pool
    // We need this because the operating system gives us random pages from its pool instead of a sequential range
    highest_frame_number = physical_page_numbers[physical_page_count - 1];

    // We reserve memory for the PFNs, but do not commit it
    // This is because we do not have all PFNs between our lowest and highest frame numbers in our page pool
    // For example, we could have frame numbers 2, 7, and 10, but not 3, 4, 5, 6, 8, or 9
    // We will only commit memory for the PFNs that we actually have in our page pool
    // While reserving memory for all of them in order to have a O(1) lookup time between a frame number and its pfn
    pfn_base = VirtualAlloc(NULL, highest_frame_number * sizeof(PFN),
                            MEM_RESERVE, PAGE_READWRITE);
    pfn_end = pfn_base + highest_frame_number * sizeof(PFN);
    NULL_CHECK(pfn_base, "initialize_pfn_metadata : could not reserve memory for pfn metadata")

    PPFN pfn;
    ULONG_PTR frame_number;

    for (ULONG64 i = 0; i < physical_page_count; i++)
    {
        frame_number = physical_page_numbers[i];
        // In order to differentiate between our different PTE states, we use a frame number of 0 to indicate that
        // the PTE has never been accessed before
        if (frame_number == 0) {
            continue;
        }

        // Commits memory for the pfn inside our reserved chunk
        LPVOID result = VirtualAlloc(pfn_base + physical_page_numbers[i], sizeof(PFN),
                                     MEM_COMMIT, PAGE_READWRITE);

        // If we could not commit memory for the pfn, we need to exit
        NULL_CHECK(result, "initialize_pfn_metadata : could not commit memory for pfn metadata")

        // Initializes the pfn
        memset(pfn_base + physical_page_numbers[i], 0, sizeof(PFN));
        pfn = pfn_from_frame_number(frame_number);
        pfn->flags.state = FREE;
        // This should be done in initialize_locks, but this is an intermediary method of locking PFNs
        // So it is pointless to move
        INITIALIZE_LOCK(pfn->lock);

        // Inserts our newly initialized pfn into the free page list
        InsertTailList(&free_page_list.entry, &pfn->entry);
        free_page_list.num_pages++;
    }

    // We are done with physical_page_numbers, so we can free it
    free(physical_page_numbers);
}

// This function initializes our page pool
VOID initialize_pages()
{
    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    // We use this array to store all the frame numbers of every page we take
    physical_page_numbers = (PULONG_PTR) malloc(physical_page_count * sizeof(ULONG_PTR));
    NULL_CHECK(physical_page_numbers, "initialize_pages : could not allocate memory for physical page numbers")

    // This is where we actually allocate the pages of memory into our page pool
    if (AllocateUserPhysicalPages(physical_page_handle,&physical_page_count,
                                  physical_page_numbers) == FALSE) {
        fatal_error("initialize_pages : could not allocate physical pages");
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {
        printf("initialize_pages : allocated only %llu pages out of %llu pages requested", physical_page_count,
               NUMBER_OF_PHYSICAL_PAGES);
    }

    // This sorts the physical page numbers in ascending order
    qsort(physical_page_numbers, physical_page_count, sizeof(ULONG_PTR), compare);
}

// This is a helper function for qsort
int compare (const void *a, const void *b) {
    return ( *(PULONG_PTR) a - *(PULONG_PTR) b );
}

VOID run_system(VOID)
{
    // This sets the event to start the system
    SetEvent(system_start_event);

    // This waits for the tests to finish running before exiting the function
    // Our controlling thread will wait for this function to finish before exiting the test and reporting stats
    WaitForMultipleObjects(NUMBER_OF_FAULTING_THREADS, faulting_handles, TRUE, INFINITE);
}

// This function fully initializes our system
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

    initialize_user_va_space();

    initialize_system_va_space();

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

    printf("deinitialize_system : system successfully deinitialized\n");
}

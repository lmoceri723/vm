#include "../include/debug.h"
volatile ULONG CHECK_INTEGRITY = 0;

#if READWRITE_LOGGING
READWRITE_LOG_ENTRY page_log[LOG_SIZE];
LONG64 readwrite_log_index = 0;
#endif

// Checks the integrity of a pfn list
// This is very helpful to use when debugging but very expensive
// This is done when a thread either acquires or is about to release a lock on a pfn list
VOID check_list_integrity(PPFN_LIST listhead, PPFN match_pfn)
{
#if DBG
    PLIST_ENTRY flink_entry;
    PPFN pfn;
    ULONG state;
    ULONG count;
    ULONG matched;

    if (CHECK_INTEGRITY == 0) {
        return;
    }
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

        // This attempts to check if all PFNs on the list share a state
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
#else
    UNREFERENCED_PARAMETER(listhead);
    UNREFERENCED_PARAMETER(match_pfn);
#endif
}

// This breaks into the debugger if possible,
// Otherwise it crashes the program
// This is only done if our state machine is irreparably broken (or attacked)
VOID fatal_error(char *msg)
{
    if (msg == NULL) {
        msg = "";
    }
    printf("\n%s", msg);

    DebugBreak();
    exit(1);
}

VOID map_pages(PVOID virtual_address, ULONG_PTR num_pages, PULONG_PTR page_array)
{
    if (MapUserPhysicalPages(virtual_address, num_pages, page_array) == FALSE) {
        printf("map_pages : could not map VA %p to page %llX\n", virtual_address, page_array[0]);
        fatal_error(NULL);
    }
}

VOID unmap_pages(PVOID virtual_address, ULONG_PTR num_pages)
{
    if (MapUserPhysicalPages(virtual_address, num_pages, NULL) == FALSE) {
        printf("unmap_pages : could not unmap VA %p to page %llX\n", virtual_address, num_pages);
        fatal_error(NULL);
    }
}

// This logs the access of a PTE or PFN whether it is a read or a write and records all relevant information
VOID log_access(ULONG is_pte, PVOID ppte_or_fn, ULONG operation)
{
    #if READWRITE_LOGGING

    READWRITE_LOG_ENTRY log_entry;
    PPTE pte;
    PPFN pfn;

    if (operation != WRITE && is_pte != IS_A_PTE) {
        return;
    }

    pte = (PPTE) ppte_or_fn;

    // See if the last two hex digits of the PTEs address are 68
    if ((ULONG64) pte & 0xFF != 0x68) {
        return;
    }

    ULONG64 bounded_log_index = InterlockedIncrement64(&readwrite_log_index) % LOG_SIZE;

    if (is_pte) {
        pte = (PPTE) ppte_or_fn;

        if (pte->entire_format == 0) {
            pfn = NULL;
        }
        else if (pte->memory_format.valid == 1) {
            ULONG64 frame_number = pte->memory_format.frame_number;
            if (frame_number > highest_frame_number) {
                printf("log_access : frame number for memory PTE %p is out of valid range during operation %lu\n", pte, operation);
                fatal_error(NULL);
            }
            pfn = pfn_from_frame_number(frame_number);
        }
        else if (pte->disc_format.on_disc == 1) {
            pfn = NULL;
        }
        else {
            ULONG64 frame_number = pte->transition_format.frame_number;
            if (frame_number > highest_frame_number) {
                printf("log_access : frame number for transition PTE %p is out of valid range\n", pte);
                fatal_error(NULL);
            }
            pfn = pfn_from_frame_number(frame_number);
        }
    }
    else {
        pfn = pfn_from_frame_number((ULONG64) ppte_or_fn);
        pte = pfn->pte;
    }

    if (pte == NULL) {
        log_entry.pte_ptr = NULL;
        log_entry.pte_val = (PTE) {0};
        log_entry.virtual_address = NULL;
    }
    else {
        log_entry.pte_ptr = pte;
        log_entry.pte_val = *pte;
        log_entry.virtual_address = va_from_pte(pte);
    }

    log_entry.pfn_ptr = pfn;
    if (pfn != NULL) {
        log_entry.pfn_val = *pfn;
        log_entry.frame_number = frame_number_from_pfn(pfn);
    }
    else {
        log_entry.pfn_val = (PFN) {0};
        log_entry.frame_number = 0;
    }

    log_entry.operation = operation;

    log_entry.entry_index = bounded_log_index;
    CaptureStackBackTrace(0, 8, log_entry.stack_trace, NULL);
    log_entry.accessing_thread_id = GetCurrentThreadId();

    page_log[bounded_log_index] = log_entry;
    #endif
}
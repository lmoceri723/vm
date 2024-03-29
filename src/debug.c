#include "../include/debug.h"

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
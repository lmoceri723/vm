#include "../include/structs.h"
#include "../include/system.h"
#include "../include/debug.h"
#include "../include/macros.h"

PPTE pte_base;
PPTE pte_end;

PPFN pfn_base;
PPFN pfn_end;
ULONG_PTR highest_frame_number;

PFN_LIST free_page_list;
PFN_LIST modified_page_list;
PFN_LIST standby_page_list;

// These functions convert between matching linear structures (pte and va) (pfn and frame number)
PPTE pte_from_va(PVOID virtual_address)
{
    // Null and out of bounds checks done for security purposes
    NULL_CHECK(virtual_address, "pte_from_va : virtual address is null")

    if ((ULONG_PTR) virtual_address > (ULONG_PTR) va_base + virtual_address_size
        || virtual_address < va_base)
    {
        fatal_error("pte_from_va : virtual address is out of valid range");
    }

    // We can compute the difference between the first va and our va
    // This will be equal to the difference between the first pte and the pte we want
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    // The compiler automatically multiplies the difference by the size of a pte, so it is not required here
    return pte_base + difference;
}

PVOID va_from_pte(PPTE pte)
{
    // Same checks done for security purposes
    NULL_CHECK(pte, "va_from_pte : pte is null")
    if (pte > pte_end || pte < pte_base)
    {
        fatal_error("va_from_pte : pte is out of valid range");
    }

    // The same math is done here but in reverse
    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);
    difference *= PAGE_SIZE;

    PVOID result = (PVOID) ((ULONG_PTR) va_base + difference);

    return result;
}

PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    NULL_CHECK((void *) frame_number, "pfn_from_frame_number : frame number is null")

    if (frame_number > highest_frame_number || frame_number <= 0)
    {
        fatal_error("pfn_from_frame_number : frame number is out of valid range");
    }

    // Again, the compiler implicitly multiplies frame number by PFN size
    return pfn_base + frame_number;
}

ULONG64 frame_number_from_pfn(PPFN pfn)
{
    NULL_CHECK(pfn, "frame_number_from_pfn : pfn is null")

    if (pfn > pfn_end || pfn < pfn_base)
    {
        fatal_error("frame_number_from_pfn : pfn is out of valid range");
    }

    return pfn - pfn_base;
}

// Removes the pfn from the list corresponding to its state
VOID remove_from_list(PPFN pfn)
{
    PPFN_LIST listhead;
    PLIST_ENTRY prev_entry;
    PLIST_ENTRY next_entry;
    PFN local;

    // Finds the list based on the page's state
    // There is no active state, and we never remove free pages in this way
    // So we know that this is either a modified or standby page
    if (pfn->flags.state == MODIFIED) {

        listhead = &modified_page_list;

    } else if (pfn->flags.state == STANDBY) {

        // This is on the standby list
        listhead = &standby_page_list;

    } else {
        fatal_error("remove_from_list : tried to remove a page from list when it was on none");
        return;
    }

    // Checks the integrity of the list before and after the remove operation
    check_list_integrity(listhead, pfn);

    local = read_pfn(pfn);

    // This removes the page from the list by erasing it from the chain of FLinks and blinks
    prev_entry = local.entry.Blink;
    next_entry = local.entry.Flink;

    prev_entry->Flink = next_entry;
    next_entry->Blink = prev_entry;

    local.entry.Flink = NULL;
    local.entry.Blink = NULL;

    write_pfn(pfn, local);

    listhead->num_pages--;

    check_list_integrity(listhead, NULL);
}

// This removes the first element from a pfn list and returns it
PPFN pop_from_list_helper(PPFN_LIST listhead)
{
    PPFN pfn;
    PLIST_ENTRY flink_entry;

    // Checks the integrity of the list before and after the remove operation
    check_list_integrity(listhead, NULL);

    flink_entry = RemoveHeadList(&listhead->entry);
    pfn = CONTAINING_RECORD(flink_entry, PFN, entry);

    listhead->num_pages--;

    check_list_integrity(listhead, NULL);

    return pfn;
}

// Returns a locked page
PPFN pop_from_list(PPFN_LIST listhead)
{
    BOOLEAN took_page = FALSE;
    PPFN peeked_page;
    PPFN taken_page;

    while (took_page == FALSE) {
        // Peek at the head of the list
        peeked_page = CONTAINING_RECORD(listhead->entry.Flink, PFN, entry);
        // Lock the pfn at the head
        lock_pfn(peeked_page);
        // Lock the list
        EnterCriticalSection(&listhead->lock);

        // If the list is empty, relinquish both locks and return
        if (listhead->num_pages == 0)
        {
            LeaveCriticalSection(&listhead->lock);
            unlock_pfn(peeked_page);
            return NULL;
        }

        // If the pages do not match up relinquish both locks and try again
        if (CONTAINING_RECORD(listhead->entry.Flink, PFN, entry) != peeked_page) {
            LeaveCriticalSection(&listhead->lock);
            unlock_pfn(peeked_page);
            continue;
        }

        // If the pages do match up, pop the page from the list
        taken_page = pop_from_list_helper(listhead);
        // Relinquish lock for list
        LeaveCriticalSection(&listhead->lock);
        took_page = TRUE;
    }

    return taken_page;
}

// This function simply adds a page to a specified list
VOID add_to_list(PPFN pfn, PPFN_LIST listhead) {
    // Inserts it on the list, checking the integrity of it before and after
    check_list_integrity(listhead, NULL);
    InsertTailList(&listhead->entry, &pfn->entry);
    listhead->num_pages++;
    check_list_integrity(listhead, pfn);
}

// This function is used to re-add a page to the head of a list
VOID add_to_list_head(PPFN pfn, PPFN_LIST listhead) {
    check_list_integrity(listhead, NULL);
    InsertHeadList(&listhead->entry, &pfn->entry);
    listhead->num_pages++;
    check_list_integrity(listhead, pfn);
}

#if 0
VOID log_pte_write(PTE initial, PTE new)
{

}

VOID log_pfn_write(PFN initial, PFN new)
{

}
#endif

// These functions are used to read and write PTEs and PFNs in a way that doesn't conflict with other threads
PTE read_pte(PPTE pte)
{
    // This atomically reads the PTE as a single 64 bit value
    // This is needed because the CPU or another concurrent faulting thread
    // Can still access this PTE in transition format and see an intermediate state
    PTE local;
    local.entire_format = *(volatile ULONG64 *) &pte->entire_format;
    return local;
}

// Write the value of a local PTE to a PTE in memory
VOID write_pte(PPTE pte, PTE local)
{
    // Now this is written as a single 64 bit value instead of in parts
    // This is needed because the cpu or another concurrent faulting thread
    // Can still access this pte in transition format and see an intermediate state
    *(volatile ULONG64 *) &pte->entire_format = local.entire_format;
}

// This strategy is not usable for PFNs because they are too large
PFN read_pfn(PPFN pfn)
{
    return *pfn;
}

VOID write_pfn(PPFN pfn, PFN local)
{
    *pfn = local;
}

// Functions to lock and unlock PTE regions and individual PFNs
// This locks the entire region of PTEs that the PTE is in
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
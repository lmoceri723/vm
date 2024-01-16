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
    if ((ULONG_PTR) virtual_address > (ULONG_PTR) va_base + virtual_address_size
        || virtual_address < va_base || virtual_address == NULL)
    {
        printf("pte_from_va : virtual address is out of valid range");
        fatal_error();
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
    if (pte == NULL || pte > pte_end || pte < pte_base)
    {
        printf("va_from_pte : pte is out of valid range");
        fatal_error();
    }

    // The same math is done here but in reverse
    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);
    difference *= PAGE_SIZE;

    PVOID result = (PVOID) ((ULONG_PTR) va_base + difference);

    return result;
}

PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    if (frame_number > highest_frame_number || frame_number <= 0)
    {
        printf("pfn_from_frame_number : frame number is out of valid range");
        fatal_error();
    }

    // Again, the compiler implicitly multiplies frame number by PFN size
    return pfn_base + frame_number;
}

ULONG64 frame_number_from_pfn(PPFN pfn)
{
    if (pfn == NULL || pfn > pfn_end || pfn < pfn_base)
    {
        printf("frame_number_from_pfn : pfn is out of valid range");
        fatal_error();
    }

    return pfn - pfn_base;
}

// Removes the pfn from the list corresponding to its state
VOID remove_from_list(PPFN pfn)
{
    PPFN_LIST listhead;
    PLIST_ENTRY prev_entry;
    PLIST_ENTRY next_entry;

    // Finds the list based on the page's state
    if (pfn->flags.state == MODIFIED) {

        listhead = &modified_page_list;

    } else if (pfn->flags.state == STANDBY) {

        // This is on the standby list
        listhead = &standby_page_list;

    } else {

        printf("Tried to remove a page from list when it was on none");
        fatal_error();
        return;
    }

    // Checks the integrity of the list before and after the remove operation
    check_list_integrity(listhead, pfn);

    // This removes the page from the list by erasing it from the chain of FLinks and blinks
    prev_entry = pfn->entry.Blink;
    next_entry = pfn->entry.Flink;

    prev_entry->Flink = next_entry;
    next_entry->Blink = prev_entry;

    pfn->entry.Flink = NULL;
    pfn->entry.Blink = NULL;

    listhead->num_pages--;

    check_list_integrity(listhead, NULL);
}

// This removes the first element from a pfn list and returns it
PPFN pop_from_list_helper(PPFN_LIST listhead)
{
    PPFN pfn;
    PLIST_ENTRY flink_entry;

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

        if (listhead->num_pages == 0)
        {
            LeaveCriticalSection(&listhead->lock);
            unlock_pfn(peeked_page);
            return NULL;
        }
        // If the frame numbers do not match up relinquish both locks and try again
        if (CONTAINING_RECORD(listhead->entry.Flink, PFN, entry) != peeked_page) {
            LeaveCriticalSection(&listhead->lock);
            unlock_pfn(peeked_page);
            continue;
        }

        // If the number's do match up, pop the page from the list
        taken_page = pop_from_list_helper(listhead);
        // Relinquish lock for list
        LeaveCriticalSection(&listhead->lock);
        took_page = TRUE;
    }

    return taken_page;
}

// This function simply adds a pfn to a specified list
VOID add_to_list(PPFN pfn, PPFN_LIST listhead) {
    // Inserts it on the list, checking the integrity of it before and after
    check_list_integrity(listhead, NULL);
    InsertTailList(&listhead->entry, &pfn->entry);
    listhead->num_pages++;
    check_list_integrity(listhead, pfn);
}

VOID add_to_list_head(PPFN pfn, PPFN_LIST listhead) {
    // Inserts it on the list, checking the integrity of it before and after
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
PTE read_pte(PPTE pte)
{
    // This atomically reads the pte as a single 64 bit value
    // This is needed because the cpu or another concurrent faulting thread
    // Can still access this pte in transition format and see an intermediate state
    PTE local;
    local.entire_format = *(volatile ULONG64 *) &pte->entire_format;
    return local;
}

// Write the value of a local pte to a pte in memory
VOID write_pte(PPTE pte, PTE local)
{
    //log_pte_write(*pte, local);

    // Now this is written as a single 64 bit value instead of in parts
    // This is needed because the cpu or another concurrent faulting thread
    // Can still access this pte in transition format and see an intermediate state
    *(volatile ULONG64 *) &pte->entire_format = local.entire_format;
}

PFN read_pfn(PPFN pfn)
{
    return *pfn;
}

VOID write_pfn(PPFN pfn, PFN local)
{
    //log_pfn_write(*pfn, local);
    *pfn = local;
}

// Functions to lock and unlock pte regions and individual PFNs
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
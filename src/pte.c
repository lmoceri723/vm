#include "Windows.h"
#include "../include/vm.h"
#include "../include/debug.h"

PPTE pte_base;
PPTE pte_end;

PCRITICAL_SECTION pte_region_locks;

// These functions convert between matching linear structures (pte and va)
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

// These functions are used to read and write PTEs and PFNs in a way that doesn't conflict with other threads
PTE read_pte(PPTE pte)
{
    // This atomically reads the PTE as a single 64 bit value
    // This is needed because the CPU or another concurrent faulting thread
    // Can still access this PTE in transition format and see an intermediate state
    PTE local;
    local.entire_format = *(volatile ULONG64 *) &pte->entire_format;

#if READWRITE_LOGGING
    log_access(IS_A_PTE, pte, READ);
#endif

    return local;
}

// Write the value of a local PTE to a PTE in memory
VOID write_pte(PPTE pte, PTE local)
{
    // Now this is written as a single 64 bit value instead of in parts
    // This is needed because the cpu or another concurrent faulting thread
    // Can still access this pte in transition format and see an intermediate state
    *(volatile ULONG64 *) &pte->entire_format = local.entire_format;
    if (local.entire_format == 0)
    {
        // The PTE is free
    }
    else if (local.memory_format.valid == 1)
    {
        // The PTE is valid
        if (local.memory_format.frame_number > highest_frame_number)
        {
            fatal_error("write_pte : frame number is out of valid range");
        }
    }
    else if (local.disc_format.on_disc == 1)
    {
        // The PTE is on disc
    }
    else
    {
        if (local.transition_format.frame_number > highest_frame_number)
        {
            fatal_error("write_pte : frame number is out of valid range");
        }
    }

#if READWRITE_LOGGING
    log_access(IS_A_PTE, pte, WRITE);
#endif
}

// Functions to lock and unlock PTE regions
// This locks the entire region of PTEs that the PTE is in
VOID lock_pte(PPTE pte)
{
    // We do not need to cast or multiply/divide by the size of a pte
    // This is because the compiler is smart enough to do this for us
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

#if READWRITE_LOGGING
    log_access(IS_A_PTE, pte, LOCK);
#endif

    EnterCriticalSection(&pte_region_locks[index]);
}

VOID unlock_pte(PPTE pte)
{
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

#if READWRITE_LOGGING
    log_access(IS_A_PTE, pte, UNLOCK);
#endif

    LeaveCriticalSection(&pte_region_locks[index]);
}

BOOLEAN try_lock_pte(PPTE pte)
{
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

    BOOLEAN result = TryEnterCriticalSection(&pte_region_locks[index]);

#if READWRITE_LOGGING
    if (result == TRUE) {
        log_access(IS_A_PTE, pte, TRY_SUCCESS);
    }
    else {
        log_access(IS_A_PTE, pte, TRY_FAIL);
    }
#endif

    return result;
}
#ifndef VM_STRUCTS_H
#define VM_STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
    ULONG64 age:3;
} VALID_PTE /*, *PVALID_PTE*/;

// This could be a pte that is on disc or a pte that has never been accessed before
// If on_disc is 0 AND disc index is zero, then we know it has never been accessed before
typedef struct {
    ULONG64 always_zero:1;
    ULONG64 disc_index:40;
    ULONG64 on_disc:1;
} INVALID_PTE/*, *PINVALID_PTE*/;

typedef struct {
    ULONG64 always_zero:1;
    ULONG64 frame_number:40;
    ULONG64 always_zero2:1;
} TRANSITION_PTE/*, *PTRANSITION_PTE*/;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disc_format;
        TRANSITION_PTE transition_format;
        ULONG64 entire_format;
    };
} PTE, *PPTE;

typedef struct {
    // States are FREE, CLEAN, ZEROED (to be added), ACTIVE, and MODIFIED
    ULONG state:3;
    // TODO these locks can be made into single bits instead of massive CRITICAL_SECTIONS
    CRITICAL_SECTION lock;
}PFN_FLAGS/*, *PPFN_FLAGS*/;

typedef struct {
    LIST_ENTRY entry;
    PPTE pte;
    PFN_FLAGS flags;
    ULONG64 disc_index;
} PFN, *PPFN;

typedef struct {
    LIST_ENTRY entry;
    ULONG_PTR num_pages;
    CRITICAL_SECTION lock;
} PFN_LIST, *PPFN_LIST;


extern PPTE pte_from_va(PVOID virtual_address);
extern PVOID va_from_pte(PPTE pte);
extern ULONG64 frame_number_from_pfn(PPFN pfn);
extern PPFN pfn_from_frame_number(ULONG64 frame_number);

extern VOID lock_pte(PPTE pte);
extern VOID unlock_pte(PPTE pte);
extern VOID lock_pfn(PPFN pfn);
extern VOID unlock_pfn(PPFN pfn);

extern PTE read_pte(PPTE pte);
extern VOID write_pte(PPTE pte, PTE pte_contents);
extern PFN read_pfn(PPFN pfn);
extern VOID write_pfn(PPFN pfn, PFN pfn_contents);

extern VOID remove_from_list(PPFN pfn, BOOLEAN holds_locks);
extern VOID add_to_list(PPFN pfn, PPFN_LIST listhead, BOOLEAN holds_locks);
extern PPFN pop_from_list(PPFN_LIST listhead, BOOLEAN holds_locks);

#endif //VM_STRUCTS_H

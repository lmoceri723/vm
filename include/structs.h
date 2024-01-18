#ifndef VM_STRUCTS_H
#define VM_STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define PAGE_SIZE                   4096

// We know that a PTE is in valid format if the valid bit is set
typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
    ULONG64 age:3;
} VALID_PTE /*, *PVALID_PTE*/;

// We know that a PTE is in disc format if the valid bit is not set and on_disc is set
typedef struct {
    ULONG64 always_zero:1;
    ULONG64 disc_index:40;
    ULONG64 on_disc:1;
} INVALID_PTE/*, *PINVALID_PTE*/;

// We know that a PTE is in transition format if the valid bit is not set and on_disc is not set
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
        // This is used to represent the entire format of a PTE as a number
        // If this number is zero, then we know that the PTE has never been accessed
        ULONG64 entire_format;
    };
} PTE, *PPTE;

typedef struct {
    // States are FREE, STANDBY, ZEROED (to be added), ACTIVE, and MODIFIED
    ULONG state:3;
    // In the future, these locks will be made into single bits instead of massive CRITICAL_SECTIONS
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

extern VOID remove_from_list(PPFN pfn);
extern VOID add_to_list(PPFN pfn, PPFN_LIST listhead);
extern VOID add_to_list_head(PPFN pfn, PPFN_LIST listhead);
extern PPFN pop_from_list(PPFN_LIST listhead);

#endif //VM_STRUCTS_H

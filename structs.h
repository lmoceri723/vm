#ifndef VM_STRUCTS_H
#define VM_STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
} VALID_PTE /*, *PVALID_PTE*/;

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
    // This creates a limit of how many disc indexes can exist,
    // Can be potentially fixed by separating this structure into two formats
    ULONG64 disc_index:23;
} INVALID_PTE/*, *PINVALID_PTE*/;

typedef struct {
    union {
        VALID_PTE hardware_format;
        INVALID_PTE software_format;
    };
} PTE, *PPTE;

typedef struct {
    // States are free, clean, zeroed, active, and modified
    ULONG state:3;
    // Age can range from 0 to 7
    ULONG age:3;
}PFN_FLAGS/*, *PPFN_FLAGS*/;

typedef struct {
    LIST_ENTRY entry;
    PPTE pte;
    PFN_FLAGS flags;
} PFN, *PPFN;

typedef struct {
    LIST_ENTRY entry;
    ULONG_PTR num_pages;
    CRITICAL_SECTION lock;
} PFN_LIST, *PPFN_LIST;

#endif //VM_STRUCTS_H

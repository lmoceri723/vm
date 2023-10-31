#ifndef VM_STRUCTS_H
#define VM_STRUCTS_H

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)

typedef struct {
    ULONG64 valid:1;
    ULONG64 on_disc:1;
    ULONG64 frame_number:40;
    ULONG64 age:3;
} VALID_PTE /*, *PVALID_PTE*/;

typedef struct {
    ULONG64 valid:1;
    ULONG64 on_disc:1;
    ULONG64 disc_index:40;
} INVALID_PTE/*, *PINVALID_PTE*/;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disc_format;
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

#endif //VM_STRUCTS_H

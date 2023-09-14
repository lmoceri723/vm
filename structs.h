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
    ULONG64 age:3;
} ACTIVE_PTE /*, *PACTIVE_PTE*/;

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
    // This creates a limit of how many disc indexes can exist,
    // Can be potentially fixed by separating this structure into two formats

    // This needs to be moved to the PFN
    ULONG64 disc_index:23;
} INVALID_PTE/*, *PINVALID_PTE*/;

typedef struct {
    ULONG64 valid:1;
    ULONG64 disc_index:40;
    // This creates a limit of how many disc indexes can exist,
    // Can be potentially fixed by separating this structure into two formats
} DISC_PTE/*, *PDISC_PTE*/;

typedef struct {
    union {
        ACTIVE_PTE active_format;
        INVALID_PTE software_format;
    };
} PTE, *PPTE;

// These need to be separate data structures so the cpu can quickly read them
typedef struct {
    PULONG64 bit_map;
    ULONG num_active_pages;

}PTE_REGION, *PPTE_REGION;

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

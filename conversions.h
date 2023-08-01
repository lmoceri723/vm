//
// Created by ltm14 on 7/17/2023.
//

#ifndef VM_TWO_CONVERSIONS_H
#define VM_TWO_CONVERSIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)
#define GB(x)                       (MB (x) * 1024)

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
} VALID_PTE /*, *PVALID_PTE*/;

typedef struct {
    ULONG64 valid:1;
    ULONG64 frame_number:40;
    ULONG64 disc_index:23;
} INVALID_PTE/*, *PINVALID_PTE*/;

typedef struct {
    union {
        VALID_PTE hardware_format;
        INVALID_PTE software_format;
    };
} PTE, *PPTE;

typedef struct {
    LIST_ENTRY entry;
    PPTE pte;
    ULONG_PTR frame_number;

    // States are free, clean, zeroed, active, and modified
    // LM Fix change both to 3 bits
    ULONG state;
    // Age can range from 0 to 7 with a higher age correlating to a higher need to be trimmed
    ULONG age;
} PFN, *PPFN;

typedef struct {
    LIST_ENTRY entry;
    ULONG_PTR num_pages;
} PFN_LIST, *PPFN_LIST;

extern PPTE pte_from_va(PVOID virtual_address);

extern PVOID va_from_pte(PPTE pte);

extern PPFN pfn_from_frame_number(ULONG64 frame_number);

#endif //VM_TWO_CONVERSIONS_H

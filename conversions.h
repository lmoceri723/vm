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
#define VIRTUAL_ADDRESS_SIZE        MB(16)
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)


extern PVOID va_base;
extern PPTE pte_base;
extern PPFN pfn_metadata;

PPTE pte_from_va(PVOID virtual_address)
{
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    return pte_base + difference;
}

PVOID va_from_pte(PPTE pte)
{
    // ULONG_PTR difference = (ULONG_PTR) pte - (ULONG_PTR) pte_base;

    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);

    difference *= PAGE_SIZE;

    return (PVOID) ((ULONG_PTR) va_base + difference);
}

PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    PPFN pfn = pfn_metadata;
    for (ULONG_PTR i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++)
    {
        if (pfn->frame_number == frame_number)
        {
            return pfn;
        }
        pfn++;
    }
    //LM Fix Fatal error
    return NULL;
}

#endif //VM_TWO_CONVERSIONS_H

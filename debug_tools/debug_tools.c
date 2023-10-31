#include <stdio.h>
#include <Windows.h>
#include "structs.h"

#define PTE_REGION_SIZE 256

PPTE pte_base;
PPFN pfn_metadata;
PVOID va_base;
ULONG_PTR virtual_address_size;

// These functions convert between matching linear structures (pte and va) (pfn and frame number)
PPTE pte_from_va(PVOID virtual_address)
{
    // Null check done for security purposes
    if ((ULONG_PTR) virtual_address > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("pte_from_va : virtual address is out of valid range");
        return NULL;
    }

    // We can compute the difference between the first va and our va
    // This will be equal to the difference between the first pte and the pte we want
    ULONG_PTR difference = (ULONG_PTR) virtual_address - (ULONG_PTR) va_base;
    difference /= PAGE_SIZE;

    // The compiler automatically multiplies the difference by the size of a pte, so it is not required here
    return pte_base + difference;
}

// This method is the same as before, but it
PVOID va_from_pte(PPTE pte)
{
    ULONG_PTR difference = (ULONG_PTR) (pte - pte_base);
    difference *= PAGE_SIZE;

    PVOID result = (PVOID) ((ULONG_PTR) va_base + difference);

    // TODO LM FIX This null check should be done with the input, not the output
    if ((ULONG_PTR) result > (ULONG_PTR) va_base + virtual_address_size)
    {
        printf("va_from_pte : virtual address is out of valid range");
        return NULL;
    }
    return result;
}

// TODO add out of range checks here
PPFN pfn_from_frame_number(ULONG64 frame_number)
{
    // Again, the compiler implicitly multiplies frame number by sizeof(PFN)
    return pfn_metadata + frame_number;
}

ULONG64 frame_number_from_pfn(PPFN pfn)
{
    return pfn - pfn_metadata;
}


ULONG64 get_pte_region_index(PPTE pte)
{
    ULONG64 index = pte - pte_base;
    index /= PTE_REGION_SIZE;

    return index;
}

void removeChar(char *str, char garbage) {

    char *src, *dst;
    for (src = dst = str; *src != '\0'; src++) {
        *dst = *src;
        if (*dst != garbage) dst++;
    }
    *dst = '\0';
}

int main (int argc, char** argv)
{
    int choice;
    PVOID va;
    PPTE pte;
    ULONG64 frame_number;
    PPFN pfn;
    ULONG64 pte_region_index;\

    char *va_base_string = "0x0000000000000000";
    char* virtual_address_size_string = "0x0000000000000000";
    char* pte_base_string = "0x0000000000000000";
    char* pfn_metadata_string = "0x0000000000000000";

    printf("\nEnter the virtual address base: ");
    scanf("%s", va_base_string);
    removeChar(va_base_string, '`');

    printf("\nEnter the virtual address size: ");
    scanf("%s", virtual_address_size_string);
    removeChar(virtual_address_size_string, '`');

    printf("\nEnter the pte base: ");
    scanf("%s", pte_base_string);
    removeChar(pte_base_string, '`');

    printf("\nEnter the pfn metadata base: ");
    scanf("%s", pfn_metadata_string);
    removeChar(pfn_metadata_string, '`');

    va_base = (PVOID) strtoull(va_base_string, NULL, 16);


    while (TRUE)
    {
        for (int i = 0; i < 10; ++i) {
            printf("\n");
        }
        // Prompt the user for a number
        printf("1: pte_from_va\n2: va_from_pte\n3: pfn_from_frame_number\n4: frame_number_from_pfn\n5: get_pte_region_index\n");
        printf("Enter an index: ");
        scanf("%i", &choice);

        switch(choice) {
            case 1 :
                printf("Enter a VA: ");
                scanf("%x", &va);
                pte = pte_from_va(va);
                printf("PTE Address: %p\n", &pte);
                break;

            case 2 :
                printf("Enter a PTE Address: ");
                scanf("%x", &pte);
                va = va_from_pte(pte);
                printf("VA Address: %p\n", &va);
                break;

            case 3 :
                printf("Enter a frame number: ");
                scanf("%x", &frame_number);
                pfn = pfn_from_frame_number(frame_number);
                printf("PFN Address: %p\n", &pfn);
                break;

            case 4 :
                printf("Enter a PFN Address: ");
                scanf("%x", &pfn);
                frame_number = frame_number_from_pfn(pfn);
                printf("Frame Number: %llu\n", frame_number);
                break;

            case 5 :
                printf("Enter a PTE Address: ");
                scanf("%x", &pte);
                pte_region_index = get_pte_region_index(pte);
                printf("PTE Region Index: %llu\n", pte_region_index);
                break;

            default :
                return 0;
        }
    }
}
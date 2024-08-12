#ifndef VM_PTE_H
#define VM_PTE_H
#include <Windows.h>

// This number is strategically chosen to be 512, as it corresponds to the number of entries in a page table
#define PTE_REGION_SIZE                          512

// With a region size of 512, we have 2MB of virtual memory per region
#define NUMBER_OF_PTE_REGIONS                    ((DESIRED_NUMBER_OF_PHYSICAL_PAGES + NUMBER_OF_DISC_PAGES) / PTE_REGION_SIZE)

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

extern PPTE pte_base;
extern PPTE pte_end;

extern CRITICAL_SECTION pte_region_locks[NUMBER_OF_PTE_REGIONS];

extern PPTE pte_from_va(PVOID virtual_address);
extern PVOID va_from_pte(PPTE pte);

extern VOID lock_pte(PPTE pte);
extern VOID unlock_pte(PPTE pte);
extern BOOLEAN try_lock_pte(PPTE pte);

extern PTE read_pte(PPTE pte);
extern VOID write_pte(PPTE pte, PTE pte_contents);

#endif //VM_PTE_H

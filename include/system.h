#ifndef VM_SYSTEM_H
#define VM_SYSTEM_H

#include "structs.h"
#include "userapp.h"

#define FREE 0
#define STANDBY 1
// The zeroed state is currently unimplemented
//#define ZEROED 2
#define MODIFIED 3
#define ACTIVE 4

#define NUMBER_OF_PHYSICAL_PAGES                 (MB (1) / PAGE_SIZE)
#define NUMBER_OF_DISC_PAGES                     (MB (16) / PAGE_SIZE)
#define NUMBER_OF_SYSTEM_THREADS                 2

#define BITS_PER_BYTE                            8
#define BITMAP_CHUNK_SIZE                        8
#define MAX_ULONG64                              ((ULONG64) - 1) // 0xFFFFFFFFFFFFFFFF
#define FULL_BITMAP_CHUNK                        MAX_ULONG64
#define PTE_REGION_SIZE                          256

// With a region size of 256, we have 1MB of virtual memory per region
#define NUMBER_OF_PTE_REGIONS                    ((NUMBER_OF_PHYSICAL_PAGES + NUMBER_OF_DISC_PAGES) / PTE_REGION_SIZE)

extern ULONG_PTR physical_page_count;
extern ULONG_PTR disc_page_count;
extern ULONG_PTR virtual_address_size;
extern PFN_LIST free_page_list;
extern PFN_LIST modified_page_list;
extern PFN_LIST standby_page_list;
extern PPTE pte_base;
extern PPTE pte_end;
extern PVOID va_base;
extern PVOID modified_write_va;
extern PVOID modified_read_va;
extern PVOID repurpose_zero_va;
extern PVOID disc_space;
extern PUCHAR disc_in_use;
extern PUCHAR disc_in_use_end;
extern PPFN pfn_base;
extern PPFN pfn_end;
extern ULONG_PTR highest_frame_number;

extern HANDLE wake_aging_event;
extern HANDLE modified_writing_event;
extern HANDLE pages_available_event;
extern HANDLE disc_spot_available_event;
extern HANDLE system_exit_event;

extern CRITICAL_SECTION pte_region_locks[NUMBER_OF_PTE_REGIONS];
extern CRITICAL_SECTION disc_in_use_lock;

extern DWORD modified_write_thread(PVOID context);
extern DWORD trim_thread(PVOID context);

extern VOID initialize_system(VOID);
extern VOID deinitialize_system(VOID);
extern VOID full_virtual_memory_test(VOID);

#endif //VM_SYSTEM_H

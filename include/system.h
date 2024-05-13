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

#define MEMORY_SIZE_IN_GB                        ((ULONG64) 8)
#define PAGE_FILE_SIZE_IN_GB                     ((ULONG64) 16)

#define NUMBER_OF_PHYSICAL_PAGES                 (GB(MEMORY_SIZE_IN_GB) / PAGE_SIZE)
#define NUMBER_OF_DISC_PAGES                     (GB(PAGE_FILE_SIZE_IN_GB) / PAGE_SIZE)

#define NUMBER_OF_SYSTEM_THREADS                 8

#define MAX_ULONG64                              ((ULONG64) - 1) // 0xFFFFFFFFFFFFFFFF
#define BITS_PER_BYTE                            8

#define BIG_BITMAP                               1
#if BIG_BITMAP
#define BITMAP_TYPE                              PULONG64
#define BITMAP_CHUNK_TYPE                        ULONG64
#define BITMAP_CHUNK_SIZE_IN_BITS                64
#define FULL_BITMAP_CHUNK                        0xFFFFFFFFFFFFFFFF
#define EMPTY_BITMAP_CHUNK                       0x0000000000000000
#define EMPTY_UNIT                               0
#define FULL_UNIT                                1
#else
#define BITMAP_TYPE                              PUCHAR
#define BITMAP_CHUNK_TYPE                        UCHAR
#define BITMAP_CHUNK_SIZE_IN_BITS                8
#define FULL_BITMAP_CHUNK                        0xFF
#define EMPTY_BITMAP_CHUNK                       0x00
#define EMPTY_UNIT                               0
#define FULL_UNIT                                1
#endif
#define MAX_FREED_SPACES_SIZE                    8192

#define PTE_REGION_SIZE                          512

// With a region size of 256, we have 1MB of virtual memory per region
#define NUMBER_OF_PTE_REGIONS                    ((NUMBER_OF_PHYSICAL_PAGES + NUMBER_OF_DISC_PAGES) / PTE_REGION_SIZE)

#define SPIN_COUNTS                              1
#if SPIN_COUNTS
#define SPIN_COUNT                               0xFFFFFF
#define INITIALIZE_LOCK(x)                       InitializeCriticalSectionAndSpinCount(&x, SPIN_COUNT)
#else
#define INITIALIZE_LOCK(x)                       InitializeCriticalSection(&x)
#endif

extern ULONG_PTR physical_page_count;
extern ULONG_PTR disc_page_count;
extern ULONG_PTR virtual_address_size;
extern PFN_LIST free_page_list;
extern PFN_LIST modified_page_list;
extern PFN_LIST standby_page_list;
extern PPTE pte_base;
extern PPTE pte_end;
extern PVOID va_base;
extern PVOID va__end;
extern PVOID modified_write_va;
extern PVOID modified_read_va;
extern PVOID repurpose_zero_va;
extern PVOID disc_space;

extern PCRITICAL_SECTION disc_in_use_locks;

extern ULONG64* freed_spaces;
extern CRITICAL_SECTION freed_spaces_lock;
extern ULONG64 freed_spaces_size;

extern ULONG64 last_checked_index;

extern BITMAP_TYPE disc_in_use;
extern ULONG64 free_disc_spot_count;
extern BITMAP_TYPE disc_in_use_end;

extern PPFN pfn_base;
extern PPFN pfn_end;
extern ULONG_PTR highest_frame_number;

extern HANDLE wake_aging_event;
extern HANDLE modified_writing_event;
extern HANDLE pages_available_event;
extern HANDLE disc_spot_available_event;
extern HANDLE system_exit_event;
extern HANDLE system_start_event;

extern CRITICAL_SECTION pte_region_locks[NUMBER_OF_PTE_REGIONS];
extern CRITICAL_SECTION modified_write_va_lock;
extern CRITICAL_SECTION modified_read_va_lock;
extern CRITICAL_SECTION repurpose_zero_va_lock;

extern DWORD modified_write_thread(PVOID context);
extern DWORD trim_thread(PVOID context);

extern ULONG64 get_disc_index(VOID);
extern VOID free_disc_index(ULONG64 disc_index, BOOLEAN holds_locks);

extern VOID initialize_system(VOID);
extern VOID run_system(VOID);
extern VOID deinitialize_system(VOID);
#endif //VM_SYSTEM_H

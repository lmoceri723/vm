#ifndef VM_TWO_SYSTEM_H
#define VM_TWO_SYSTEM_H

#include "structs.h"

// This value is carefully picked as it fits into our field for frame number of our invalid pte format
#define PAGE_ON_DISC (ULONG_PTR) 0xFFFFFFFFFF

#define FREE 0
#define CLEAN 1
#define ZEROED 2
#define MODIFIED 3
#define ACTIVE 4

extern ULONG_PTR physical_page_count;
extern ULONG_PTR disc_page_count;
extern ULONG_PTR virtual_address_size;
extern PFN_LIST free_page_list;
extern PFN_LIST modified_page_list;
extern PFN_LIST standby_page_list;
extern PFN_LIST active_page_list[8];
extern PPTE pte_base;
extern PPTE pte_end;
extern PVOID va_base;
extern PVOID modified_write_va;
extern PVOID modified_read_va;
extern PVOID disc_space;
extern PUCHAR disc_in_use;
extern PUCHAR disc_end;
extern PPFN pfn_metadata;

extern BOOLEAN initialize_system(VOID);
extern PVOID allocate_memory(PULONG_PTR num_bytes);
extern BOOLEAN page_fault_handler(BOOLEAN faulted, PVOID arbitrary_va);
extern BOOLEAN full_virtual_memory_test(VOID);
extern PPFN pfn_from_frame_number(ULONG64 frame_number);

#endif //VM_TWO_SYSTEM_H

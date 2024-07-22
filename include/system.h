#ifndef VM_SYSTEM_H
#define VM_SYSTEM_H
#include "structs.h"
#include "pagefile.h"
#include "userapp.h"

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

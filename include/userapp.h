#ifndef VM_USERAPP_H
#define VM_USERAPP_H
#include <stdio.h>
#include <Windows.h>
#include "hardware.h"
#include "debug.h"

extern PULONG faulting_thread_ids;

extern ULONG64 num_trims;

extern PVOID allocate_memory(PULONG_PTR num_bytes);
// Eventually an API I code will do this instead of directly passing this to the page fault handler
extern VOID page_fault_handler(PVOID arbitrary_va, PFAULT_STATS stats);

extern DWORD faulting_thread(PVOID context);
#endif //VM_USERAPP_H

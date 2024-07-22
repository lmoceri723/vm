// Created by ltm14 on 8/2/2023.

#ifndef VM_USERAPP_H
#define VM_USERAPP_H
#include <stdio.h>
#include <Windows.h>
#include "hardware.h"


typedef struct {
    ULONG64 num_first_accesses;
    ULONG64 num_reaccesses;
    ULONG64 num_faults;
    ULONG64 num_fake_faults;
} FAULT_STATS, *PFAULT_STATS;

extern FAULT_STATS fault_stats[NUMBER_OF_FAULTING_THREADS];
extern ULONG faulting_thread_ids[NUMBER_OF_FAULTING_THREADS];

extern HANDLE system_start_event;
extern HANDLE system_exit_event;

extern ULONG64 num_trims;

extern PVOID allocate_memory(PULONG_PTR num_bytes);
// Eventually an API I code will do this instead of directly passing this to the page fault handler
extern VOID page_fault_handler(PVOID arbitrary_va, PFAULT_STATS stats);

extern DWORD faulting_thread(PVOID context);
#endif //VM_USERAPP_H

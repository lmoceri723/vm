// Created by ltm14 on 8/2/2023.

#ifndef VM_USERAPP_H
#define VM_USERAPP_H
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define PAGE_SIZE                   4096
#define NUMBER_OF_FAULTING_THREADS  8
#define MB(x)                       ((x) * 1024 * 1024)
#define GB(x)                       ((x) * 1024 * 1024 * 1024)


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

extern PVOID allocate_memory(PULONG_PTR num_bytes);
// Eventually an API I code will do this instead of directly passing this to the page fault handler
extern VOID page_fault_handler(PVOID arbitrary_va, PFAULT_STATS stats);
extern DWORD faulting_thread(PVOID context);

#endif //VM_USERAPP_H

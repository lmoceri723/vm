// Created by ltm14 on 8/2/2023.

#ifndef VM_USERAPP_H
#define VM_USERAPP_H
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#define PAGE_SIZE                   4096
#define MB(x)                       ((x) * 1024 * 1024)
//#define GB(x)                       (MB (x) * 1024)


extern ULONG64 fake_faults;
extern ULONG64 num_faults;

extern PVOID allocate_memory(PULONG_PTR num_bytes);
// Eventually an API I code will do this instead of directly passing this to the page fault handler
extern VOID page_fault_handler(PVOID arbitrary_va);

#endif //VM_USERAPP_H

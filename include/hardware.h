#ifndef HARDWARE_H
#define HARDWARE_H
#include <Windows.h>

#define BITS_PER_BYTE                            ((ULONG64) 8)

#define PAGE_SIZE                                ((ULONG64) 4096)
#define PAGE_SIZE_IN_BITS                        (PAGE_SIZE * BITS_PER_BYTE)

#define REGISTER_SIZE                            ((ULONG64) 8)
#define REGISTER_SIZE_IN_BITS                    (REGISTER_SIZE * BITS_PER_BYTE)

#define CACHE_LINE_SIZE                          ((ULONG64) 64)
#define CACHE_LINE_SIZE_IN_BITS                  (CACHE_LINE_SIZE * BITS_PER_BYTE)

#define MAX_ULONG64                              ((ULONG64) - 1) // 0xFFFFFFFFFFFFFFFF

#define KB(x)                                    ((ULONG64) ((x) * 1024))
#define MB(x)                                    ((ULONG64) (KB(x) * 1024))
#define GB(x)                                    ((ULONG64) (MB(x) * 1024))

#define MEMORY_SIZE_IN_GB                        ((ULONG64) 8)
#define PAGE_FILE_SIZE_IN_GB                     ((ULONG64) MEMORY_SIZE_IN_GB * 2)

#define DESIRED_NUMBER_OF_PHYSICAL_PAGES         (GB(MEMORY_SIZE_IN_GB) / PAGE_SIZE)
#define NUMBER_OF_DISC_PAGES                     (GB(PAGE_FILE_SIZE_IN_GB) / PAGE_SIZE)

#define NUMBER_OF_FAULTING_THREADS               8

#endif //HARDWARE_H


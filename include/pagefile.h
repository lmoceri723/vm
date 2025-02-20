#ifndef PAGEFILE_H
#define PAGEFILE_H
#include <Windows.h>
#include "hardware.h"

// The number of pages on the page file
#define PAGE_FILE_SIZE_IN_BYTES                  (NUMBER_OF_DISC_PAGES * PAGE_SIZE)
#define PAGE_FILE_SIZE_IN_BITS                   (PAGE_FILE_SIZE_IN_BYTES * BITS_PER_BYTE)

#define PAGEFILE_RELATIVE_PATH "\\pagefile\\pagefile.sys"

#define BITMAP_CHUNK                             ULONG64
#define PBITMAP_CHUNK                            PULONG64
#define BITMAP_CHUNK_SIZE                        ((ULONG64) sizeof(ULONG64))
#define BITMAP_CHUNK_SIZE_IN_BITS                ((ULONG64) BITMAP_CHUNK_SIZE * BITS_PER_BYTE)

#define FULL_BITMAP_CHUNK                        0xFFFFFFFFFFFFFFFF
#define EMPTY_BITMAP_CHUNK                       0x0000000000000000

#define EMPTY_UNIT                               ((ULONG64) 0)
#define FULL_UNIT                                ((ULONG64) 1)

#define BITMAP_SIZE_IN_BITS                      NUMBER_OF_DISC_PAGES
#define BITMAP_SIZE_IN_BYTES                     (BITMAP_SIZE_IN_BITS / BITS_PER_BYTE)
#define BITMAP_SIZE_IN_PAGES                     max( ((ULONG64) 1), (BITMAP_SIZE_IN_BYTES / PAGE_SIZE))
#define BITMAP_SIZE_IN_CHUNKS                    (BITMAP_SIZE_IN_BYTES / BITMAP_CHUNK_SIZE)

#define BITMAP_REGION_SIZE_IN_BYTES              ((ULONG64) 4096)
#define BITMAP_REGION_SIZE_IN_BITS               (BITMAP_REGION_SIZE_IN_BYTES * BITS_PER_BYTE)

#define BITMAP_SIZE_IN_REGIONS                   (BITMAP_SIZE_IN_BYTES / BITMAP_REGION_SIZE_IN_BYTES)

#define DISC_INDEX_FAIL_CODE                     0xFFFFFFFFFFFFFFFF

#define MAX_FREED_SPACES_SIZE                    ((ULONG64) 1024)

extern HANDLE pagefile_handle;
extern PVOID page_file;

extern PBITMAP_CHUNK page_file_bitmap;
extern PBITMAP_CHUNK page_file_bitmap_end;

extern volatile LONG64 free_disc_spot_count;

extern PULONG64 freed_spaces;
extern volatile LONG64 freed_spaces_size;

extern volatile LONG64 last_checked_index;

extern ULONG64 get_disc_indices(PULONG64 disc_indices, ULONG64 num_indices);
extern VOID free_disc_index(ULONG64 disc_index);
VOID free_disc_indices(PULONG64 disc_indices, ULONG64 num_indices, ULONG64 start_index);

VOID write_to_pagefile(ULONG64 disc_index, PVOID src_va);
VOID read_from_pagefile(ULONG64 disc_index, PVOID dst_va);
#endif //PAGEFILE_H

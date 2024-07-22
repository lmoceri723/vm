#include <stdio.h>
#include <Windows.h>
#include "../include/system.h"
#include "../include/pagefile.h"
#include "../include/debug.h"


extern PVOID page_file;

PBITMAP_CHUNK page_file_bitmap;
PBITMAP_CHUNK page_file_bitmap_end;
volatile LONG64 free_disc_spot_count;

PULONG64 freed_spaces;
volatile LONG64 freed_spaces_size;


volatile LONG64 last_checked_index;


ULONG64 add_freed_index(ULONG64 disc_index);
ULONG64 get_freed_index();

ULONG64 disc_index_to_region(ULONG64 disc_index)
{
    ULONG64 region = disc_index / ((ULONG64) BITMAP_REGION_SIZE_IN_BYTES * BITMAP_REGION_SIZE_IN_BITS);
    assert(region < BITMAP_SIZE_IN_REGIONS);
    return region;
}

// Called with the freed_spaces_lock and the region lock held
// Returns the updated ULONG64 with the bits set to 1
ULONG64 search_chunk_for_free_spots(PULONG64 disc_spot, ULONG64 first_index)
{
    ULONG64 expected = *disc_spot;
    if (expected == FULL_BITMAP_CHUNK) {
        return DISC_INDEX_FAIL_CODE;
    }

    // Calculate the number of indexes we can store in our stack
    ULONG64 return_index = DISC_INDEX_FAIL_CODE;
    ULONG count = 0;

    // Variables for the compare exchange

    ULONG64 desired = FULL_BITMAP_CHUNK;
    ULONG64 compex_return;


    // Fill out the entire chunk
    while (TRUE) {
        compex_return = InterlockedCompareExchange64((volatile PLONG64) disc_spot, desired, expected);

        // Check if the ULONG64 has changed since we last read it
        if (compex_return == expected) {
            break;
        }

        // If it was changed to FULL_BITMAP_CHUNK, then we can't add any more indices
        if (compex_return == FULL_BITMAP_CHUNK) {
            return DISC_INDEX_FAIL_CODE;
        }

        expected = compex_return;
    }

    // Intial expected = 0001
    // desired = 1111
    // ~expected = 1110
    // desired & ~expected = 1110
    ULONG64 disc_spot_local = desired & ~expected;
    ULONG64 add_result;
    // Iterate over the old value and find the indices of all the free spots we just filled
    for (int bit = 0; bit < BITMAP_CHUNK_SIZE_IN_BITS; bit++)
    {
        ULONG64 bit_mask = (FULL_UNIT << bit);
        if ((disc_spot_local & bit_mask) != EMPTY_UNIT) {

            if (count == 0) {
                return_index = first_index + bit;
            } else {
                add_result = add_freed_index(first_index + bit);
                if (add_result == DISC_INDEX_FAIL_CODE) {
                    break;
                }
            }
            disc_spot_local &= ~bit_mask;

            count++;
        }
    }
    // At the end of this, disc_spot_local equals every bit we could not consume

    disc_spot_local = ~disc_spot_local;
    InterlockedAnd64((volatile PLONG64) disc_spot, disc_spot_local);

    LONG64 last_checked_index_local = first_index;
    InterlockedExchange64(&last_checked_index, last_checked_index_local);

    return return_index;
}

// Called with the freed_spaces_lock held, only needs to worry about the bitmap region lock
ULONG64 search_region_for_free_spots(ULONG64 region)
{
    ULONG64 return_index = DISC_INDEX_FAIL_CODE;

    // No lock is gotten here to make reading faster, we only need it to write once we find a free spot

    // For each char in the region
    PULONG64 disc_region_base = page_file_bitmap + region * BITMAP_REGION_SIZE_IN_BYTES / BITMAP_CHUNK_SIZE;
    PULONG64 disc_region_end = disc_region_base + BITMAP_REGION_SIZE_IN_BYTES / BITMAP_CHUNK_SIZE;

    for (PULONG64 disc_spot = disc_region_base; disc_spot < disc_region_end; disc_spot++)
    {
        if (*disc_spot == FULL_BITMAP_CHUNK)
        {
            continue;
        }

        // Compute the lowest disc index
        ULONG64 lowest_disc_index = (region * BITMAP_REGION_SIZE_IN_BYTES) + (disc_spot - disc_region_base) * BITMAP_CHUNK_SIZE;
        lowest_disc_index *= BITS_PER_BYTE;

        return_index = search_chunk_for_free_spots(disc_spot, lowest_disc_index);
        if (return_index != DISC_INDEX_FAIL_CODE)
        {
            return return_index;
        }

    }
    return DISC_INDEX_FAIL_CODE;
}


ULONG64 get_disc_index()
{
    ULONG64 return_index = DISC_INDEX_FAIL_CODE;

    return_index = get_freed_index();
    if (return_index != DISC_INDEX_FAIL_CODE) {
        return return_index;
    }

    ULONG64 first_bitmap_region = disc_index_to_region(last_checked_index);

    for (ULONG64 region = 0; region < BITMAP_SIZE_IN_REGIONS; region++)
    {
        ULONG64 search_region = (first_bitmap_region + region) % BITMAP_SIZE_IN_REGIONS;
        return_index = search_region_for_free_spots(search_region);
        if (return_index != DISC_INDEX_FAIL_CODE)
        {
            return return_index;
        }
    }

    // If we reach this point, we have checked all the spots and found none and return MAX_ULONG64
    return DISC_INDEX_FAIL_CODE;
}

// This function will double insert a disc index if it is called twice with the same index
// Currently this is not a problem, as this function is only called with locks held that prevent this from happening
VOID free_disc_index(ULONG64 disc_index)
{
    PULONG64 disc_spot;
    ULONG64 index_in_cluster;

    // Add the disc index to freed_spaces if there is space
    if (add_freed_index(disc_index) == DISC_INDEX_FAIL_CODE) {
        // This grabs the actual chunk (ULONG64) that holds the bit we need to change
        disc_spot = page_file_bitmap + disc_index / BITMAP_CHUNK_SIZE_IN_BITS;

        // This gets the bit's index inside the char
        index_in_cluster = disc_index % BITMAP_CHUNK_SIZE_IN_BITS;

        ULONG64 mask = ~(FULL_UNIT << index_in_cluster);

        InterlockedAnd64((volatile PLONG64) disc_spot, mask);

        // We set the bit to be zero by comparing it using a LOGICAL AND (&=) with all ones,
        // Except for a zero at the place we want to set as zero.
        // We compute this comparison value by taking one positive bit (1)
        // And left-shifting (<<) it by index_in_cluster bits to its corresponding position in the char
        // If the position is two, then our 1 would become 001. Five more bits would then be added to the end
        // By the compiler in order to match the size of the char it is being compared to (00100000)
        // Then we flip these bits using the (~) operator to get our comparison value

        // Example: (actual byte) 11011101 &= 11111011 (comparison value)
        // Result: 11011(0)01
        // The zero surrounded by parenthesis is the bit we change; all others are preserved

        // This asserts that the disc space is not already free
        //assert((spot_cluster & (FULL_UNIT << (index_in_cluster))) != EMPTY_UNIT);

        // If there is no space, then we want to move our last checked index back to the on_stack index so that we don't
        // lose it, as it will otherwise sit in a bubble of all the spaces before last_checked_index
        if (disc_index < last_checked_index)
        {
            InterlockedExchange64(&last_checked_index, disc_index);
        }
    }

    InterlockedIncrement64(&free_disc_spot_count);
    SetEvent(disc_spot_available_event);
}

ULONG64 add_freed_index(ULONG64 disc_index)
{
    ULONG64 expected = freed_spaces_size;
    if (expected == MAX_FREED_SPACES_SIZE)
    {
        return DISC_INDEX_FAIL_CODE;
    }
    ULONG64 desired = expected + 1;
    ULONG64 compex_return = ~expected;
    while (compex_return != expected)
    {
        compex_return = InterlockedCompareExchange64((volatile PLONG64) &freed_spaces_size, desired, expected);

        if (compex_return != expected)
        {
            expected = compex_return;
            if (expected == MAX_FREED_SPACES_SIZE)
            {
                return DISC_INDEX_FAIL_CODE;
            }
            desired = expected + 1;
        }
        else
        {
            break;
        }
    }

    freed_spaces[compex_return] = disc_index;
    return disc_index;
}

ULONG64 get_freed_index()
{
    ULONG64 expected = freed_spaces_size;
    if (expected == 0)
    {
        return DISC_INDEX_FAIL_CODE;
    }
    ULONG64 desired = expected - 1;
    ULONG64 compex_return = ~expected;
    while (compex_return != expected)
    {
        compex_return = InterlockedCompareExchange64((volatile PLONG64) &freed_spaces_size, desired, expected);

        if (compex_return != expected)
        {
            expected = compex_return;
            if (expected == 0)
            {
                return DISC_INDEX_FAIL_CODE;
            }
            desired = expected - 1;
        }
        else
        {
            break;
        }
    }
    ULONG64 index = freed_spaces[compex_return - 1];

    return index;
}


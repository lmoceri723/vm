#include "../include/system.h"
#include "../include/debug.h"

PCRITICAL_SECTION disc_in_use_locks;

ULONG64* freed_spaces;
CRITICAL_SECTION freed_spaces_lock;
ULONG64 freed_spaces_size;

ULONG64 last_checked_index;

ULONG64 disc_index_to_region(ULONG64 disc_index);
VOID add_freed_index(ULONG64 disc_index);
ULONG64 get_freed_index();

ULONG64 get_disc_index()
{
    ULONG64 return_index = MAX_ULONG64;
    ULONG64 indices_in_cluster;

    EnterCriticalSection(&freed_spaces_lock);
    if (freed_spaces_size != 0)
    {
        return_index = get_freed_index();
        LeaveCriticalSection(&freed_spaces_lock);
        return return_index;
    }
    LeaveCriticalSection(&freed_spaces_lock);


    ULONG64 first_bitmap_region = disc_index_to_region(last_checked_index);

    // TODO am i mixing here? is disc_page_count what we want?
    for (ULONG64 i = first_bitmap_region; i < disc_page_count; i++)
    {
        // No lock is gotten here to make reading faster, we only need it to write once we find a free spot

        // For each char in the region
        PULONG64 disc_region_base = disc_in_use + i * PAGE_SIZE / sizeof (*disc_in_use);
        PULONG64 disc_region_end = disc_region_base + PAGE_SIZE / sizeof (*disc_in_use);

        for (BITMAP_TYPE disc_spot = disc_region_base; disc_spot < disc_region_end; disc_spot++)
        {
            // If the ULONG64 is full, we skip it
            if (*disc_spot != FULL_BITMAP_CHUNK)
            {
                // If it's not full, we lock it and ensure it didn't become full while we were waiting
                EnterCriticalSection(&disc_in_use_locks[i]);

                if (*disc_spot == FULL_BITMAP_CHUNK)
                {
                    LeaveCriticalSection(&disc_in_use_locks[i]);
                    continue;
                }

                // Compute the lowest disc index
                ULONG64 lowest_disc_index = (i * PAGE_SIZE) + (disc_spot - disc_region_base) * sizeof (*disc_in_use);
                lowest_disc_index *= BITS_PER_BYTE;

                EnterCriticalSection(&freed_spaces_lock);

                // Calculate how many zeroed bits are in the char
                ULONG64 zeroed_bits = (ULONG) __popcnt64(~(*disc_spot));

                // Calculate the amount of indexes we can store in our stack
                ULONG64 available_indexes = MAX_FREED_SPACES_SIZE - freed_spaces_size;

                // Boolean for first iteration
                BOOLEAN first_iteration = TRUE;

                // 58 zeroes + 110 111
                BITMAP_CHUNK_TYPE spot_cluster;
                while ((first_iteration == TRUE) || (zeroed_bits != 0 && available_indexes != 0))
                {
                    assert(zeroed_bits != 0)

                    spot_cluster = *disc_spot;
                    // 58 zeroes + 110 111


                    // This is the number of leading ones before the first zero
                    ULONG64 offset = 0;
                    while (TRUE)
                    {
                        indices_in_cluster = __lzcnt64((spot_cluster));
                        if (indices_in_cluster == 0)
                        {
                            spot_cluster <<= 1;
                            offset++;
                        }
                        else
                        {
                            break;
                        }
                    }

                    // lzcnt = 58
                    if (indices_in_cluster > available_indexes)
                    {
                        indices_in_cluster = available_indexes;
                    }

                    // Bits can go off the end if we left shift 64 times, so we need to handle that case separately
                    if (indices_in_cluster == 64)
                    {
                        *disc_spot = FULL_BITMAP_CHUNK;
                    }
                    else
                    {
                        // Set the leading bits to one
                        ULONG64 mask = ((1 << indices_in_cluster) - 1);
                        ULONG64 shift = (64 - indices_in_cluster);
                        shift -= offset;
                        *disc_spot |= (mask << shift);
                    }
                    // 11111100...11111111


                    // FIRST TIME
                    // ((1 << indicies_in_cluster) - 1) << (64 - indicies_in_cluster)
                    // 000001000000... 1 << indicies_in_cluster (58)
                    // 000000111111... subtract 1 to get all ones after
                    // 111111...000000  left shift by 64 - indicies_in_cluster to get the leading ones

                    // Get the specific indexes
                    // leading indicies_in_cluster bits and convert them to disc indicies

                    for (ULONG64 index = 0; index < indices_in_cluster; index++) {
                        ULONG64 disc_index = lowest_disc_index + 64 - (index + 1);

                        if (first_iteration == TRUE)
                        {
                            first_iteration = FALSE;
                            return_index = disc_index;
                        } else {
                            add_freed_index(disc_index);
                        }
                    }

                    // Set the last checked index
                    last_checked_index = lowest_disc_index + 64;

                    // Decrement the global count of free disc spots
                    free_disc_spot_count -= indices_in_cluster;

                    // Decrement the amount of zeroed bits
                    zeroed_bits -= indices_in_cluster;
                    // Decrement the amount of available indexes
                    available_indexes -= indices_in_cluster;
                }

                LeaveCriticalSection(&freed_spaces_lock);

                LeaveCriticalSection(&disc_in_use_locks[i]);

                return return_index;
            }
            else
            {
                continue;
            }
        }
    }
    // If we reach this point, we have checked all the spots and found none and return MAX_ULONG64
    return return_index;
}


VOID free_disc_index(ULONG64 disc_index, BOOLEAN holds_locks)
{
    BITMAP_TYPE disc_spot;
    BITMAP_CHUNK_TYPE spot_cluster;
    ULONG64 index_in_cluster;

    ULONG64 region = disc_index_to_region(disc_index);
    if (!holds_locks)
    {
        EnterCriticalSection(&disc_in_use_locks[region]);
    }

    // This grabs the actual char (byte) that holds the bit we need to change
    disc_spot = disc_in_use + disc_index / BITMAP_CHUNK_SIZE_IN_BITS;
    spot_cluster = *disc_spot;

    // This gets the bit's index inside the char
    index_in_cluster = disc_index % BITMAP_CHUNK_SIZE_IN_BITS;

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
    assert(spot_cluster & (FULL_UNIT << (index_in_cluster)))

    spot_cluster &= ~(FULL_UNIT << (index_in_cluster));

    // Write the char back out after the bit has been changed
    *disc_spot = spot_cluster;
    free_disc_spot_count++;

    // Add the disc index to freed_spaces if there is space
    if (freed_spaces_size < MAX_FREED_SPACES_SIZE)
    {
        add_freed_index(disc_index);
    }
    // If there is no space, then we want to move our last checked index back to the freed index so that we don't
    // lose it, as it will otherwise sit in a bubble of all the spaces before last_checked_index
    else
    {
        if (disc_index < last_checked_index)
        {
            last_checked_index = disc_index;
        }
    }

    if (!holds_locks)
    {
        LeaveCriticalSection(&disc_in_use_locks[region]);
    }
    SetEvent(disc_spot_available_event);
}

// These two functions should only be called with locks and never called outside of this file
VOID add_freed_index(ULONG64 disc_index)
{
    freed_spaces[freed_spaces_size] = disc_index;
    freed_spaces_size++;
}

ULONG64 get_freed_index()
{
    if (freed_spaces_size == 0)
    {
        return MAX_ULONG64;
    }

    ULONG64 index = freed_spaces[freed_spaces_size - 1];
    freed_spaces_size--;

    return index;
}

ULONG64 disc_index_to_region(ULONG64 disc_index)
{
    return disc_index / PAGE_SIZE;
}


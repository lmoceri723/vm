#include <Windows.h>
#include "../include/vm.h"
#include "../include/scheduler.h"

MOD_WRITE_TIME mod_write_times[MOD_WRITE_TIMES_TO_TRACK];
ULONG64 mod_write_time_index;

ULONG64 available_pages[SECONDS_TO_TRACK];
ULONG64 available_pages_index;

ULONG64 num_batches_to_write;

MOD_WRITE_TIME average_mod_write_times(VOID)
{
    MOD_WRITE_TIME average;
    ULONG64 total_duration = 0;
    ULONG64 total_pages = 0;

    ULONG64 i;
    for (i = 0; i < ARRAYSIZE(mod_write_times); i++)
    {
        if (mod_write_times[i].num_pages == 0)
        {
            break;
        }
        total_duration += mod_write_times[i].duration;
        total_pages += mod_write_times[i].num_pages;
    }

    average.duration = total_duration / i;
    average.num_pages = total_pages / i;

    return average;
}

VOID track_mod_write_time(ULONG64 duration, ULONG64 num_pages)
{
    mod_write_times[mod_write_time_index].duration = duration;
    mod_write_times[mod_write_time_index].num_pages = num_pages;
    mod_write_time_index = (mod_write_time_index + 1) % MOD_WRITE_TIMES_TO_TRACK;
}


ULONG64 average_page_consumption(VOID) {
    ULONG64 total = 0;

    ULONG64 count = 0;
    for (count = 0; count < ARRAYSIZE(available_pages); count++) {
        if (available_pages[count] != MAXULONG64) {
            total += available_pages[count];
            count++;
        }
        else {
            break;
        }
    }

    ULONG64 result = total / count;
    if (result == 0) {
        return 1;
    }
    return result;
}

VOID track_available_pages(ULONG64 num_pages)
{
    // Compute the previous index
    ULONG64 previous_index;
    if (available_pages_index == 0) {
        previous_index = SECONDS_TO_TRACK - 1;
    }
    else {
        previous_index = available_pages_index - 1;
    }

    // If the previous index is uninitialized, we know that this is the first time we are tracking this
    // So we set the available pages to the total number of pages - the number of pages we are tracking
    if (available_pages[previous_index] == MAXULONG64) {
        available_pages[available_pages_index] = physical_page_count - num_pages;
    }
    else {
        available_pages[available_pages_index] = available_pages[previous_index] - num_pages;
    }

    available_pages_index = (available_pages_index + 1) % SECONDS_TO_TRACK;
}

// This controls the thread that constantly writes pages to disc when prompted by other threads
// In the future this should use a fraction of the CPU if the system cannot give it a full core
DWORD task_scheduling_thread(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    // This thread needs to be able to react to handles for waking as well as exiting
    HANDLE handles[1];

    handles[0] = system_exit_event;

    // This waits for the system to start before doing anything
    WaitForSingleObject(system_start_event, INFINITE);
    // TODO LM FIX GIVE THIS THREAD ITS OWN STATUS LINE

    while (TRUE)
    {
        // Wait for 1 second always unless the system is exiting
        ULONG index = WaitForMultipleObjects(ARRAYSIZE(handles), handles, FALSE, 1000);
        if (index == 0)
        {
            set_modified_status("modified write thread exited");
            break;
        }

        while (TRUE) {
            // This count could be totally broken, as the counts of free and standby page counts are from different times
            // We can trust them both individually at that time but not together
            ULONG64 available_pages = *(volatile ULONG_PTR *) (&free_page_list.num_pages) +
                                     *(volatile ULONG_PTR *) (&standby_page_list.num_pages);

            // Track the current number of available pages
            track_available_pages(available_pages);

            // This finds the average number of pages that have been consumed in the last 16 seconds
            ULONG64 average_pages_consumed = average_page_consumption();

            MOD_WRITE_TIME average = average_mod_write_times();
            ULONG64 per_page_cost = average.duration / average.num_pages;

            // First, find how long it will take us to empty our modified list completely
            ULONG64 time_to_empty_modified = modified_page_list.num_pages / per_page_cost;
            // Second, find how long until we have no more free or standby pages
            ULONG64 time_until_no_pages = available_pages / average_pages_consumed;

            if (time_to_empty_modified < time_until_no_pages) {
                // do a write every per_page_cost writes
                ULONG64 time_per_operation = per_page_cost;
                // sleep(time_per_operation)
            }
            else {

            // do a write every seconds remaining / time to drain
            ULONG64 time_per_operation;
            // sleep(time_per_operation - time_to_drain)

            // if there isnt enough time to drain, do 1 s / per page cost writes
            // otherwise do the inverse
        }
    }

    return 0;
}
#include <stdio.h>
#include <Windows.h>
#include <time.h>
#include "../include/structs.h"
#include "../include/system.h"
#include "../include/debug.h"

#pragma comment(lib, "advapi32.lib")
// LM FIX FIGURE OUT ATTRIBUTE UNUSED AND FIX OTHER WARNINGS
// TODO LM FIX ENSURE ALL PTE AND PFN WRITES ARE DONE WITH OUR NEW METHOD


PPFN get_free_page(VOID);
PPFN read_page_on_disc(PPTE pte, PPFN free_page);
VOID free_disc_space(ULONG64 disc_index);

ULONG_PTR virtual_address_size;
ULONG_PTR physical_page_count;
ULONG_PTR disc_page_count;
PVOID va_base;
PVOID modified_write_va;
PVOID modified_read_va;
PVOID repurpose_zero_va;
PVOID disc_space;
PUCHAR disc_in_use;
PUCHAR disc_in_use_end;

// This breaks into the debugger if possible,
// Otherwise it crashes the program
// This is only done if our state machine is irreparably broken (or attacked)
VOID fatal_error(VOID)
{
    printf("\n fatal error");
    DebugBreak();
    exit(1);
}

// This is how we get pages for new virtual addresses as well as old ones only exist on the paging file
PPFN get_free_page(VOID) {
    PPFN free_page = NULL;

    // First, we check the free page list for pages
    if (free_page_list.num_pages != 0) {
        // Once we allow users to free memory, we will need to zero this too
        free_page = pop_from_list(&free_page_list);
        //assert(free_page == NULL || free_page->flags.state == FREE)
    }

    // We want to be 100% sure of our check here because we expect this case to happen almost every time
    // Also because we do not want to use our last resort option unless absolutely necessary

    // This is where we take pages from the standby list and reallocate their physical pages for our new va to use
    if (free_page == NULL && standby_page_list.num_pages != 0)
    {
        free_page = pop_from_list(&standby_page_list);
        if (free_page == NULL) {
            fatal_error();
        }

        PPTE other_pte = free_page->pte;
        ULONG64 other_disc_index = free_page->disc_index;
        // We want to start writing entire PTEs instead of writing them bit by bit

        // We hold this PTEs PFN lock which allows us to access this PTE here without a lock
        PTE local = *other_pte;
        local.disc_format.on_disc = 1;
        local.disc_format.disc_index = other_disc_index;
        write_pte(other_pte, local);

        // This is where we clear the previous contents off of the repurposed page
        // This is important as it can corrupt the new user's data if not entirely overwritten,
        // It also would allow a program to see another program's memory (HUGE SECURITY VIOLATION)
        ULONG_PTR frame_number = frame_number_from_pfn(free_page);
        if (MapUserPhysicalPages(repurpose_zero_va, 1, &frame_number) == FALSE) {
            printf("page_fault_handler : could not map VA %p to page %llu\n", repurpose_zero_va,
                   frame_number);
            fatal_error();
        }

        memset(repurpose_zero_va, 0, PAGE_SIZE);

        if (MapUserPhysicalPages(repurpose_zero_va, 1, NULL) == FALSE) {
            printf("page_fault_handler : could not unmap VA %p to page %llu\n", repurpose_zero_va,
                   frame_number);
            fatal_error();
        }
    }

    // This is a last resort option when there are no available pages
    // We wake the aging thread and send this information to the fault handler
    // Which then waits on a page to become available
    // Aging is done here no matter what
    SetEvent(wake_aging_event);
    // Once we have depleted a page from the free/standby list, it is a good idea to consider aging
    return free_page;
}

// This reads a page from the paging file and writes it back to memory
PPFN read_page_on_disc(PPTE pte, PPFN free_page)
{
    // We don't need a pfn lock here because this page is not on a list
    // And therefore is not visible to any other threads
    ULONG_PTR frame_number = frame_number_from_pfn(free_page);

    // We map these pages into our own va space to write contents into them, and then put them back in user va space
    if (MapUserPhysicalPages(modified_read_va, 1, &frame_number) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        fatal_error();
    }

    // This would be a disc driver that does this read and write in a real operating system
    PVOID source = (PVOID) ((ULONG_PTR) disc_space + (pte->disc_format.disc_index * PAGE_SIZE));
    memcpy(modified_read_va, source, PAGE_SIZE);

    if (MapUserPhysicalPages(modified_read_va, 1, NULL) == FALSE) {

        printf ("full_virtual_memory_test : could not unmap VA %p to page %llX\n", modified_read_va,
                frame_number_from_pfn(free_page));
        fatal_error();
    }

    // Set the bit at disc_index in disc in use to be 0 to reuse the disc spot
    free_disc_space(pte->disc_format.disc_index);
    return free_page;
}

VOID free_disc_space(ULONG64 disc_index)
{
    PUCHAR disc_spot;
    UCHAR spot_cluster;
    ULONG index_in_cluster;

    EnterCriticalSection(&disc_in_use_lock);

    // This grabs the actual char (byte) that holds the bit we need to change
    disc_spot = disc_in_use + disc_index / BITMAP_CHUNK_SIZE;
    spot_cluster = *disc_spot;

    // This gets the bit's index inside the char
    index_in_cluster = disc_index % BITMAP_CHUNK_SIZE;

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
    assert(spot_cluster & (1<<(index_in_cluster)))

    spot_cluster &= ~(1<<(index_in_cluster));

    // Write the char back out after the bit has been changed
    *disc_spot = spot_cluster;

    LeaveCriticalSection(&disc_in_use_lock);
    SetEvent(disc_spot_available_event);
}

// This is where we handle any access or fault of a page
VOID page_fault_handler(PVOID arbitrary_va)
{
    PPTE pte;
    PTE pte_contents;
    PPFN pfn;
    PFN pfn_contents;
    ULONG64 frame_number;

    // Pages go through the handler regardless of whether they have faulted or not
    // This is because even if a page is accessed without a fault, it's age in the pte must be updated

    // First, we need to get the actual pte corresponding to the va we faulted on
    pte = pte_from_va(arbitrary_va);
    if (pte == NULL)
    {
        printf("page_fault_handler : cannot get pte from va");
        fatal_error();
    }

    // This order of operations is very important
    // A pte lock MUST sequentially come before a pfn lock
    // This is because we must lock the pte corresponding to a faulted va in order to handle its fault
    // At this point we do not know the pfn and cannot find it without a pte lock
    lock_pte(pte);
    pte_contents = read_pte(pte);

    // This is where the age is updated on an active page that has not actually faulted
    // We refer to this as a fake fault
    // We know this page is active because its valid bit is set, which only exists in a memory format pte
    if (pte_contents.memory_format.valid == 1)
    {
        // LM MULTITHREADING FIX write a function that uses a dictionary to update debug stats and uses interlocked increment
        fake_faults++;
        // We do this check to avoid a pte write
        if (pte_contents.memory_format.age == 0)
        {
            unlock_pte(pte);
            return;
        }

        pte_contents.memory_format.age = 0;
        write_pte(pte, pte_contents);

        unlock_pte(pte);
        return;
    }

    // At this point, we know that the page actually faulted
    num_faults++;

    // If the entre pte is zeroed, it means that this is a brand new va that has never been accessed.
    // Technically, we only need to know that on_disc and the field at the position of
    // Frame_number/disc_index are zero, but this is easier to check and more easily understood
    // We know now that we need to get a free/standby page and map it to this va
    if (pte_contents.entire_format == 0)
    {
        // Get_free_page now returns a locked page, so we do not need to do it here
        pfn = get_free_page();

        // This occurs when we get_free_page fails to find us a free page
        // When this happens, we release our lock on this pte and wait for pages to become available
        // Once we are able to map a page to this va, we return, which lets the thread fault on this va again
        if (pfn == NULL) {
            unlock_pte(pte);
            WaitForSingleObject(pages_available_event, INFINITE);
            return;
        }
    }
    // At this point, we know that this pte is in transition or disc format, as the valid bit is clear
    // We can now safely check for the on_disc bit, if it is set it signifies to us that our pte is in disc format
    // It has been trimmed, written to disc, and its physical page has been reused
    // We need to read this page from the paging file and remap it to the va
    // We call this a hard fault, as we have to read from disc in order to handle it
    // We want to minimize hard faults, as they takes exponentially longer than other types of faults to resolve
    else if (pte_contents.disc_format.on_disc == 1) {

        pfn = get_free_page();
        if (pfn == NULL) {
            unlock_pte(pte);
            WaitForSingleObject(pages_available_event, INFINITE);
            return;
        }

        // This is where we actually read the page from the disc and write its contents to our new page
        read_page_on_disc(pte, pfn);

        // At this point, we know that our pte is in transition format, as it is not active or on disc
        // This va must have been trimmed, but its pfn has not been repurposed
        // All we need to do is remove it from the standby or modified lists now
        // This is called a soft fault, as this can be resolved all in memory and doesn't require disc reads/writes
        // This is our ideal type of fault, as it takes the shortest amount of time to fix
    } else {
        // This will unlink our page from the standby or modified list
        // It uses the PFNs information to determine which list it is on

        pfn = pfn_from_frame_number(pte_contents.transition_format.frame_number);

        // Because we need to acquire a pfn before a PTE inside our get_free_page function,
        // We can no longer trust a transition format PTE's contents until we also lock its pfn
        // After locking the pfn we need to reread the pte and ensure that it did not become disc format
        lock_pfn(pfn);

        pte_contents = read_pte(pte);
        if (pte_contents.disc_format.on_disc == 1)
        {
            return;
        }

        assert(pte_contents.transition_format.always_zero == 0)
        assert(pte_contents.transition_format.always_zero2 == 0)
        assert(pte_contents.transition_format.frame_number == frame_number_from_pfn(pfn))
        assert(pfn->flags.state == STANDBY || pfn->flags.state == MODIFIED)

        if (pfn->flags.state == MODIFIED) {
            EnterCriticalSection(&modified_page_list.lock);
            remove_from_list(pfn);
            LeaveCriticalSection(&modified_page_list.lock);

        } else /*(pfn->flags.state == STANDBY) */{

            EnterCriticalSection(&standby_page_list.lock);
            remove_from_list(pfn);
            // Freeing the space here and updating the pfn lower down
            free_disc_space(pfn->disc_index);
            LeaveCriticalSection(&standby_page_list.lock);
        }
    }
    // We now have the page we need, we just need to correctly map it now to the pte and return

    pte_contents = read_pte(pte);
    pfn_contents = read_pfn(pfn);
    frame_number = frame_number_from_pfn(pfn);

    pte_contents.memory_format.frame_number = frame_number_from_pfn(pfn);
    pte_contents.memory_format.valid = 1;
    pte_contents.memory_format.age = 0;
    write_pte(pte, pte_contents);

    pfn_contents.pte = pte;
    pfn_contents.flags.state = ACTIVE;
    pfn_contents.disc_index = 0;
    write_pfn(pfn, pfn_contents);

    // This is a Windows API call that confirms the changes we made with the OS
    // We have already mapped this va to this page on our side, but the OS also needs to do the same on its side
    // This is necessary as this is a user mode program
    if (MapUserPhysicalPages(arbitrary_va, 1, &frame_number) == FALSE) {

        printf("page_fault_handler : could not map VA %p to page %llX\n", arbitrary_va,
               frame_number_from_pfn(pfn));
        fatal_error();
    }

    unlock_pfn(pfn);
    unlock_pte(pte);
}

// Eventually, we will move this to an api.c and api.h file
// That way programs can use our memory manager without knowing or having to know anything about how it works
PVOID allocate_memory(PULONG_PTR num_bytes)
{
    *num_bytes = virtual_address_size;
    return va_base;
}

// This main is likely to be moved to userapp.c in the future
// Figure out attribute unused
int main (int argc, char** argv)
{
     /* This is where we initialize and test our virtual memory management state machine

     We control the entirety of virtual and physical memory management with only two exceptions
        We ask the operating system for all physical pages we use to store data
        (AllocateUserPhysicalPages)
        We ask the operating system to connect one of our virtual addresses to one of our physical pages
        (MapUserPhysicalPages)

     In a real kernel program, we would do these things ourselves,
     But the operating system (for security reasons) does not allow us to.

     But we do everything else commonly features in a memory manager
     Including: maintaining translation tables, PTE and PFN data structures, management of physical pages,
     Virtual memory operations like handling page faults, materializing mappings, freeing them, trimming them,
     Writing them out to a paging file, bringing them back from the paging file, protecting them, and much more */

    initialize_system();

    full_virtual_memory_test();

    deinitialize_system();
    return 0;
}

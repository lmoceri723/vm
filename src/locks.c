#include <Windows.h>
#include <stdio.h>

#define LOCKS_SPIN 1
#if LOCKS_SPIN
#define MAX_SPIN_COUNT 8192
#endif

typedef struct {
    SHORT lock_semaphore;
    // 0 is spin lock, 1 is event lock
} LOCK, *PLOCK;

// typedef LONG NTSTATUS;
// #define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
//
// extern NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
//
// VOID wait_for_nanoseconds(LONG nanoseconds) {
//     LARGE_INTEGER interval;
//     // Convert nanoseconds to 100-nanosecond intervals (negative value for relative time)
//     interval.QuadPart = -(nanoseconds / 100);
//
//     NtDelayExecution(FALSE, &interval);
// }

VOID initialize_lock(PLOCK lock) {
    lock->lock_semaphore = 0;
}

VOID acquire_lock(PLOCK lock) {
    ULONG64 spin_count = 0;
    while (TRUE) {
        // Read the lock as a ULONG
        SHORT expected = lock->lock_semaphore;
        SHORT desired = 1;

        if (InterlockedCompareExchange16((volatile short *) &lock->lock_semaphore, desired, expected) == 0)
            return;

#if LOCKS_SPIN
        spin_count++;
        if (spin_count > MAX_SPIN_COUNT) {
            // TODO Wait for 1 microsecond
            Sleep(1);
        }
#endif
    }
}

BOOLEAN try_acquire_lock(PLOCK lock) {
    SHORT expected = lock->lock_semaphore;
    SHORT desired = 1;

    if (InterlockedCompareExchange16((volatile short *) &lock->lock_semaphore, desired, expected) == 0) {
        return TRUE;
    }
    return FALSE;
}

VOID release_lock(PLOCK lock) {
    lock->lock_semaphore = 0;
}
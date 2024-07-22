#ifndef VM_DEBUG_H
#define VM_DEBUG_H
#include "structs.h"

#define NULL_CHECK(x, msg)       if (x == NULL) {fatal_error(msg); }

// Creates a central switch to turn debug mode on/off
#define DBG                1

#if DBG
#define assert(x)       if (!(x)) { printf ("Assertion failed: %s, file %s, line %d\n", #x, __FILE__, __LINE__); DebugBreak(); }
#else
#define assert(x)
#endif

#define SPIN_COUNTS                              0

#if SPIN_COUNTS
#define SPIN_COUNT                               0xFFFFFF
#define INITIALIZE_LOCK(x)                       InitializeCriticalSectionAndSpinCount(&x, SPIN_COUNT)
#else
#define INITIALIZE_LOCK(x)                       InitializeCriticalSection(&x)
#endif

extern VOID check_list_integrity(PPFN_LIST listhead, PPFN match_pfn);
// LM TODO move these to a better place
extern VOID fatal_error(char *msg);
extern VOID map_pages(PVOID user_va, ULONG_PTR page_count, PULONG_PTR page_array);
extern VOID unmap_pages(PVOID user_va, ULONG_PTR page_count);

#endif //VM_DEBUG_H

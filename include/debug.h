//
// Created by ltm14 on 11/15/2023.
//

#ifndef VM_DEBUG_H
#define VM_DEBUG_H
#include "structs.h"

#if 0
#define DBG                TRUE
#endif

#if DBG
#define assert(x)       if (!(x)) { printf ("Assertion failed: %s, file %s, line %d\n", #x, __FILE__, __LINE__); DebugBreak(); }
#else
#define assert(x)
#endif

extern VOID check_list_integrity(PPFN_LIST listhead, PPFN match_pfn);

#endif //VM_DEBUG_H

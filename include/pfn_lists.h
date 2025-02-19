#ifndef PFN_LISTS_H
#define PFN_LISTS_H
#include <Windows.h>
#include "pfn.h"

typedef struct {
    LIST_ENTRY entry;
    ULONG_PTR num_pages;
    CRITICAL_SECTION lock;
} PFN_LIST, *PPFN_LIST;

extern PFN_LIST free_page_list;
extern PFN_LIST modified_page_list;
extern PFN_LIST standby_page_list;

extern VOID remove_from_list(PPFN pfn);
extern VOID add_to_list_tail(PPFN pfn, PPFN_LIST listhead);
extern VOID add_to_list_head(PPFN pfn, PPFN_LIST listhead);
extern PPFN pop_from_list_head(PPFN_LIST listhead);
extern PFN_LIST batch_pop_from_list_head(PPFN_LIST listhead, PPFN_LIST batch_list, ULONG64 batch_size);

extern VOID initialize_listhead(PPFN_LIST listhead);
extern BOOLEAN is_list_empty(PPFN_LIST listhead);
VOID link_list_to_tail(PPFN_LIST first, PPFN_LIST last);

#endif //PFN_LISTS_H

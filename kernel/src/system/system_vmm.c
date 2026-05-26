typedef struct System_Vmm_Page_Range {
    Uintptr start;
    Int     len;
} System_Vmm_Page_Range;

typedef struct System_Vmm_Page_Ranges {
    struct System_Vmm_Page_Range *next;
    // Forms a linked list of ranges's that contain free slots
    struct System_Vmm_Page_Range *next_free;
    struct System_Vmm_Page_Range *_reserved;
    Int count;

    System_Vmm_Page_Range ranges[254];
} System_Vmm_Page_Ranges;

USED
global U8 _system_paging_vmm_page_ranges_check_correct_size[size_of(System_Vmm_Page_Ranges) == SYSTEM_PAGE_SIZE ? 1 : -1];

typedef struct System_Vmm {
    System_Paging_Table    *table;
    System_Vmm_Page_Ranges *ranges;
} System_Vmm;

/// Simple physical page allocator
/// Design:
///  Bitmap for querying page availibility
///  Stack for allocation and deallocation
///  One physical page allocator handles exactly one range of physical addresses
/// Traps:
///  None of the returned pages are zeroed, this has to be done in the VMM.
/// Future improvements:
///  We could maybe switch to a buddy allocator at some point,
///   but I don't think that makes sense as long as we don't need physical pages
///   next to each other.
///  Make the implementation atomic for smp support.
///   Shouldn't be to hard, it should probably atomically fetch und update the
///   stack_top variable. Edits should probably only happen based on that variable.

typedef struct System_Physical_Allocator {
    struct System_Physical_Allocator *next;
    struct System_Physical_Allocator *next_16mb;
    struct System_Physical_Allocator *next_4gb;

    Int     size;
    Int     stack_top;
    U64     *pages_bitmap; // bitmask that says if a page is used or not
    U32     *pages_stack;  // stack for O(1) alloc and free
    Uintptr address_base;
} System_Physical_Allocator;

typedef struct System_Pa_Page_Index {
    Int bitmap_index;
    Int bit_index;
} System_Pa_Page_Index;

internal System_Pa_Page_Index system_pa_calculate_page_index(System_Physical_Allocator *pa /* for bounds checking */, Int page) {
    kassert(0 <= page && page < pa->size);

    return (System_Pa_Page_Index){
        page / 64,
        page % 64
    };
}

typedef struct System_Pa_Size_Specification {
    Int stack_size;
    Int bitmap_size;
} System_Pa_Size_Specification;

internal System_Pa_Size_Specification system_pa_calculate_size_specification(Int page_count) {
    kassert(page_count <= U32_MAX);

    System_Pa_Size_Specification spec = { 0 };
    spec.stack_size  = page_count * size_of(U32);
    spec.bitmap_size = ((page_count + (size_of(U64) * 8 - 1)) / (size_of(U64) * 8)) * size_of(U64);
    return spec;
}

internal Int system_pa_calculate_required_size(Int page_count) {
    kassert(page_count <= U32_MAX);

    System_Pa_Size_Specification spec = system_pa_calculate_size_specification(page_count);
    return size_of(System_Physical_Allocator) + spec.stack_size + spec.bitmap_size;
}

internal void system_pa_set_used(System_Physical_Allocator *pa, Int page_index) {
    System_Pa_Page_Index page = system_pa_calculate_page_index(pa, page_index);

    // set page as used
    pa->pages_bitmap[page.bitmap_index] |= (U64)1 << page.bit_index;
}

internal System_Physical_Allocator *system_pa_init(void *address, Uintptr address_base, Int metadata_pages, Int page_count) {
    kassert(page_count <= U32_MAX);
    kassert(0          <  page_count);
    kassert(0          <  metadata_pages);

    zero(address, metadata_pages * SYSTEM_PAGE_SIZE);
    System_Pa_Size_Specification spec = system_pa_calculate_size_specification(page_count);

    System_Physical_Allocator *pa = address;

    U8 *data  = address;
    data     += size_of(System_Physical_Allocator);

    pa->pages_bitmap  = (U64 *)data;
    data             += spec.bitmap_size;

    pa->pages_stack   = (U32*)data;

    pa->size = page_count;

    Int stack_index = 0;

    for (U32 i = (U32)metadata_pages;
        i < (U32)page_count;
        i++, stack_index++) {

        pa->pages_stack[stack_index] = i;
    }

    pa->stack_top = stack_index - 1;

    for (Int i = 0; i < metadata_pages; i++) {
        system_pa_set_used(pa, i);
    }

    pa->address_base = address_base;

    return address;
}

// WARN: Do *NOT* rely on the 0 that is returned when allocation fails, only use the _ok_ parameter
//       Zero is a valid physical page
internal Uintptr system_pa_alloc(System_Physical_Allocator *pa, bool *ok) {
    kassert(ok);

    if (pa->stack_top < 0 || pa->size <= 0) {
        *ok = false;
        return 0;
    }

    U32 offset = pa->pages_stack[pa->stack_top];
    system_pa_set_used(pa, (Int)offset);

    pa->stack_top -= 1;

    *ok = true;
    Uintptr address = (Uintptr)(offset * SYSTEM_PAGE_SIZE) + pa->address_base;
    kassert(pa->address_base <= address && address < pa->address_base + ((Uintptr)pa->size * SYSTEM_PAGE_SIZE));
    return address;
}

internal bool system_pa_is_free(System_Physical_Allocator *pa, Uintptr address) {
    kassert(pa->address_base <= address && address < pa->address_base + ((Uintptr)pa->size * SYSTEM_PAGE_SIZE));

    Int page_index = (Int)(address - pa->address_base) / SYSTEM_PAGE_SIZE;
    System_Pa_Page_Index page = system_pa_calculate_page_index(pa, page_index);

    return !(pa->pages_bitmap[page.bitmap_index] & ((U64)1 << page.bit_index));
}

internal void system_pa_free(System_Physical_Allocator *pa, Uintptr address) {
    printf("1");
    kassert(!system_pa_is_free(pa, address));
    printf("2");
    kassert(pa->stack_top+1 <  pa->size);
    printf("3");
    kassert(pa->stack_top   >= -1);
    printf("4");

    Int page_index = (Int)(address - pa->address_base) / SYSTEM_PAGE_SIZE;

    pa->stack_top                  += 1;
    pa->pages_stack[pa->stack_top]  = (U32)page_index;

    System_Pa_Page_Index page = system_pa_calculate_page_index(pa, page_index);

    pa->pages_bitmap[page.bitmap_index] &= ~((U64)1 << page.bit_index);
}

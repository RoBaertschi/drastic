typedef struct System_Paging_Address {
    Uintptr physical;
    Uintptr virtual;
} System_Paging_Address;

typedef void System_Paging_Table;

LIMINE_REQUEST
static volatile struct limine_memmap_request system_paging_memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUEST
static volatile struct limine_executable_address_request system_paging_exe_addr_request = {
    .id       = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

global String system_paging_memmap_string[] = {
    [LIMINE_MEMMAP_USABLE]                 = STR("USABLE"),
    [LIMINE_MEMMAP_RESERVED]               = STR("RESERVED"),
    [LIMINE_MEMMAP_ACPI_RECLAIMABLE]       = STR("ACPI_RECLAIMABLE"),
    [LIMINE_MEMMAP_ACPI_NVS]               = STR("ACPI_NVS"),
    [LIMINE_MEMMAP_BAD_MEMORY]             = STR("BAD_MEMORY"),
    [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = STR("BOOTLOADER_RECLAIMABLE"),
    [LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = STR("EXECUTABLE_AND_MODULES"),
    [LIMINE_MEMMAP_FRAMEBUFFER]            = STR("FRAMEBUFFER"),
    [LIMINE_MEMMAP_RESERVED_MAPPED]        = STR("RESERVED_MAPPED"),
};

typedef enum System_Physical_Page_Kind {
    SYSTEM_PHYSICAL_PAGE_NORMAL,
    SYSTEM_PHYSICAL_PAGE_16MB,
    SYSTEM_PHYSICAL_PAGE_4GB,
} System_Physical_Page_Kind;

typedef struct System_Paging {
    // sentinel is a global so that it has a stable address
    // NOTE: sentinel always points to itself, to allow for not handling
    //       NULL. Instead you detect it if the next ptr == the current ptr
    System_Physical_Allocator pa_sentinel;

    struct System_Physical_Allocator *first;
    struct System_Physical_Allocator *first_16mb;
    struct System_Physical_Allocator *first_4gb;

    struct System_Physical_Allocator *last;
    struct System_Physical_Allocator *last_16mb;
    struct System_Physical_Allocator *last_4gb;

    // Pages that are not required to be in the first 16mb should use memory later, save on that
    struct System_Physical_Allocator *first_non_16mb;
} System_Paging;

global System_Paging system_paging;

internal bool system_paging_pa_is_sentinel(
    struct System_Physical_Allocator *pa) {

    return pa == pa->next;
}

USED
internal Uintptr system_paging_physical_alloc(System_Physical_Page_Kind kind, bool *ok) {
    // TODO(robin): benchmark

    kassert(ok);
    // TODO(robin): cache current physical allocator used for better performance


    switch (kind) {
    case SYSTEM_PHYSICAL_PAGE_NORMAL:
        for (System_Physical_Allocator *current = system_paging.first_non_16mb;
                !system_paging_pa_is_sentinel(current);
                current = current->next) {

            Uintptr result = system_pa_alloc(current, ok);
            if (*ok) {
                return result;
            }
        }

        // try 16mb parts, TODO(robin): log
        fallthrough;
    case SYSTEM_PHYSICAL_PAGE_16MB:
        for (System_Physical_Allocator *current = system_paging.first_16mb;
                !system_paging_pa_is_sentinel(current);
                current = current->next_16mb) {

            Uintptr result = system_pa_alloc(current, ok);
            if (*ok) {
                return result;
            }
        }
        break;

    case SYSTEM_PHYSICAL_PAGE_4GB:
        for (System_Physical_Allocator *current = system_paging.first_4gb;
                !system_paging_pa_is_sentinel(current);
                current = current->next_4gb) {

            Uintptr result = system_pa_alloc(current, ok);
            if (*ok) {
                return result;
            }
        }
        break;
    }

    *ok = false;
    return 0;
}

internal void system_paging_physical_free(Uintptr address) {
    // TODO(robin): benchmark

    // TODO(robin): do binary search to find correct physical allocator faster
    //              or support some better way to find it

    for (System_Physical_Allocator *current = system_paging.first;
            !system_paging_pa_is_sentinel(current);
            current = current->next) {

        Uintptr address_base = current->address_base;

        if (address_base <= address && address < address_base + ((Uintptr)current->size * SYSTEM_PAGE_SIZE)) {
            system_pa_free(current, address);
            return;
        }
    }

    printf("PAGING: could not free physical address at %p\n", (void*)address);
    kpanic(STR("PAGING: invalid physical page free"));
}

#define SYSTEM_PAGING_AMD64_FLAG_P    (U64)BIT(0)  // present
#define SYSTEM_PAGING_AMD64_FLAG_RW   (U64)BIT(1)  // Read/Write
#define SYSTEM_PAGING_AMD64_FLAG_US   (U64)BIT(2)  // User/Supervisor
#define SYSTEM_PAGING_AMD64_FLAG_PWT  (U64)BIT(3)  // Write-Through
#define SYSTEM_PAGING_AMD64_FLAG_PCD  (U64)BIT(4)  // Cache Disable
#define SYSTEM_PAGING_AMD64_FLAG_A    (U64)BIT(5)  // Accessed

#define SYSTEM_PAGING_AMD64_FLAG_AVL1 (U64)BIT(6)  // Available bit 1
#define SYSTEM_PAGING_AMD64_FLAG_D    (U64)BIT(6)  // Dirty

#define SYSTEM_PAGING_AMD64_FLAG_PS   (U64)BIT(7) // Reserved depending on table
#define SYSTEM_PAGING_AMD64_FLAG_PAT  (U64)BIT(7) // Page Attribute Table

#define SYSTEM_PAGING_AMD64_FLAG_AVL2 (U64)BIT(8)  // Available bit 2
#define SYSTEM_PAGING_AMD64_FLAG_G    (U64)BIT(8)  // Global

#define SYSTEM_PAGING_AMD64_FLAG_AVL3 (U64)BIT(9)  // Available bit 3
#define SYSTEM_PAGING_AMD64_FLAG_AVL4 (U64)BIT(10) // Available bit 4

#define SYSTEM_PAGING_AMD64_FLAG_XD (U64)BIT(63) // Execute Disable

#define SYSTEM_PAGING_AMD64_FLAG_ADDRESS ((((U64)1 << 48) -1) & ~(((U64)1 << 12) - 1))

USED
internal void system_paging_pml4_entry(U64 *target, void *target_address, U64 flags) {
    // PS is not supported on a pml4
    kassert(!(flags & SYSTEM_PAGING_AMD64_FLAG_PS));

    *target = (((U64)(Uintptr)target_address) & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) | flags;
}

internal void system_paging_pdpt_entry(U64 *target, void *target_address, U64 flags) {
    *target = (((U64)(Uintptr)target_address) & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) | flags;
}

internal void system_paging_pd_entry(U64 *target, void *target_address, U64 flags) {
    *target = (((U64)(Uintptr)target_address) & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) | flags;
}

internal void system_paging_pt_entry(U64 *target, void *target_address, U64 flags) {
    *target = (((U64)(Uintptr)target_address) & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) | flags;
}

#define SYSTEM_PAGING_FLAG_WRITE (U64)BIT(0)
#define SYSTEM_PAGING_FLAG_EXEC  (U64)BIT(1)
#define SYSTEM_PAGING_FLAG_USER  (U64)BIT(2)

internal U64 system_paging_amd64_flag_convert(U64 flags) {
    U64 value = 0;

    if (flags & SYSTEM_PAGING_FLAG_WRITE) {
        value |= SYSTEM_PAGING_AMD64_FLAG_RW;
    }

    if (flags & SYSTEM_PAGING_FLAG_USER) {
        value |= SYSTEM_PAGING_AMD64_FLAG_US;
    }

    if (!(flags & SYSTEM_PAGING_FLAG_EXEC)) {
        value |= SYSTEM_PAGING_AMD64_FLAG_XD;
    }

    return value;
}

internal Uintptr system_paging_entry__new_physical(U64 hhdm_offset) {
    bool ok = false;
    Uintptr new_address = system_paging_physical_alloc(SYSTEM_PHYSICAL_PAGE_NORMAL, &ok);

    if (!ok) {
        kpanic(STR("PAGING: could not allocate physical page for page mapping"));
    }
    zero((void *)(new_address + hhdm_offset), SYSTEM_PAGE_SIZE);
    return new_address;
}

#define SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(variable) kassert(((variable) & (SYSTEM_PAGE_SIZE - 1)) == 0)

internal U64 *system_paging_entry(System_Paging_Table *table, Uintptr virtual) {
    SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(virtual);
    kassert(table);

    U64 hhdm_offset     = system_hhdm_request.response->offset;
    U64 parent_flags    = SYSTEM_PAGING_AMD64_FLAG_P | SYSTEM_PAGING_AMD64_FLAG_RW;
    U64 *pml4           = table;
    U64 virtual_address = (U64)virtual;
    U64 *entry = &pml4[(virtual_address >> 39) & ((1 << 9) - 1)];

    if (!(*entry & SYSTEM_PAGING_AMD64_FLAG_P)) {
        Uintptr new_address = system_paging_entry__new_physical(hhdm_offset);
        system_paging_pml4_entry(entry, (void*)new_address, parent_flags);
    }

    U64 *pdpt       = (U64*)((*entry & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) + hhdm_offset);
    U64 *pdpt_entry = &pdpt[(virtual_address >> 30) & ((1 << 9) - 1)];

    if (!(*pdpt_entry & SYSTEM_PAGING_AMD64_FLAG_P)) {
        Uintptr new_address = system_paging_entry__new_physical(hhdm_offset);
        system_paging_pdpt_entry(pdpt_entry, (void*)new_address, parent_flags);
    }

    U64 *pd       = (U64*)((*pdpt_entry & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) + hhdm_offset);
    U64 *pd_entry = &pd[(virtual_address >> 21) & ((1 << 9) - 1)];

    if (!(*pd_entry & SYSTEM_PAGING_AMD64_FLAG_P)) {
        Uintptr new_address = system_paging_entry__new_physical(hhdm_offset);
        system_paging_pd_entry(pd_entry, (void*)new_address, parent_flags);
    }

    U64 *pt       = (U64*)((*pd_entry & SYSTEM_PAGING_AMD64_FLAG_ADDRESS) + hhdm_offset);
    U64 *pt_entry = &pt[(virtual_address >> 12) & ((1 << 9) - 1)];

    return pt_entry;
}

internal void system_paging_map(System_Paging_Table *table, Uintptr physical, Uintptr virtual, U64 flags) {
    SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(virtual);
    SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(physical);

    U64 converted_flags = system_paging_amd64_flag_convert(flags);
    U64 *pt_entry       = system_paging_entry(table, virtual);
    system_paging_pt_entry(pt_entry, (void*)physical, converted_flags | SYSTEM_PAGING_AMD64_FLAG_P);
    asm_invlpg((void*)virtual);
}

internal void system_paging_unmap(System_Paging_Table *table, Uintptr virtual) {
    SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(virtual);

    U64 *pt_entry = system_paging_entry(table, virtual);
    *pt_entry     = 0;
    asm_invlpg((void *)virtual);
}

internal System_Paging_Address system_paging_page_clone(void *page, U64 hhdm_offset) {
    System_Paging_Address address = { 0 };

    bool ok = false;
    address.physical = system_paging_physical_alloc(SYSTEM_PHYSICAL_PAGE_NORMAL, &ok);
    address.virtual  = address.physical + hhdm_offset;

    if (!ok) {
        kpanic(STR("PAGING: could not allocate physical page for page mapping"));
    }

    memcpy((void *)(address.virtual), page, SYSTEM_PAGE_SIZE);

    return address;
}

internal U64 system_paging_table_entry_update_address(U64 entry, U64 new_address) {
    SYSTEM_PAGING_ASSERT_ADDRESS_ALIGNED(new_address);
    U64 zeroed  = ~SYSTEM_PAGING_AMD64_FLAG_ADDRESS;
    entry      &= zeroed;
    entry      |= new_address & SYSTEM_PAGING_AMD64_FLAG_ADDRESS;
    return entry;
}

internal U64 *system_paging_table_page_entry_clone(U64 *entry, U64 hhdm_offset) {
    U64 old_physical_address = *entry
        & SYSTEM_PAGING_AMD64_FLAG_ADDRESS;
    U64 *old = (U64*)(
        old_physical_address + hhdm_offset
    );
    System_Paging_Address new_address = system_paging_page_clone(
        old, hhdm_offset
    );

    *entry = system_paging_table_entry_update_address(
        *entry, (U64)new_address.physical
    );
    return (U64 *)(void *)new_address.virtual;
}

internal System_Paging_Address system_paging_table_clone(System_Paging_Table *table) {
    U64 hhdm_offset = system_hhdm_request.response->offset;

    System_Paging_Address cloned_table_address = system_paging_page_clone(table, hhdm_offset);

    Int entries_per_page = SYSTEM_PAGE_SIZE / size_of(U64);

    U64 *pml4 = (U64*)cloned_table_address.virtual;
    for (Int i = 0; i < entries_per_page; i++) {
        if (!(pml4[i] & SYSTEM_PAGING_AMD64_FLAG_P)) {
            // Skip pages that are not present
            continue;
        }

        U64 *new_pdpt = system_paging_table_page_entry_clone(
            &pml4[i], hhdm_offset
        );

        for (Int j = 0; j < entries_per_page; j++) {
            if (!(new_pdpt[j] & SYSTEM_PAGING_AMD64_FLAG_P)) {
                // Skip pages that are not present
                continue;
            }

            if (new_pdpt[j] & SYSTEM_PAGING_AMD64_FLAG_PS) {
                continue;
            }

            U64 *new_pd = system_paging_table_page_entry_clone(
                &new_pdpt[j], hhdm_offset
            );

            for (Int k = 0; k < entries_per_page; k++) {
                if (!(new_pd[k] & SYSTEM_PAGING_AMD64_FLAG_P)) {
                    // Skip pages that are not present
                    continue;
                }

                if (new_pd[k] & SYSTEM_PAGING_AMD64_FLAG_PS) {
                    continue;
                }

                system_paging_table_page_entry_clone(
                    &new_pd[k], hhdm_offset
                );
            }
        }
    }

    return cloned_table_address;
}

// WARN: to avoid regression:
//       do not append the sentinel at the end, it is already set and would break.
internal void system_paging_setup(U64 stack_top) {
    kassert(system_paging_memmap_request.response != NULL);

    struct System_Physical_Allocator base = { 0 };

    // The sentinel always points to itself
    system_paging.pa_sentinel.next      = &system_paging.pa_sentinel;
    system_paging.pa_sentinel.next_16mb = &system_paging.pa_sentinel;
    system_paging.pa_sentinel.next_4gb  = &system_paging.pa_sentinel;

    struct System_Physical_Allocator *last      = &base;
    struct System_Physical_Allocator *last_16mb = &base;
    struct System_Physical_Allocator *last_4gb  = &base;

    struct System_Physical_Allocator *first_non_16mb = &system_paging.pa_sentinel;

    U64 hhdm_offset = system_hhdm_request.response->offset;

    U64 physical_stack_top = stack_top - hhdm_offset;

    for (U64 i = 0; i < system_paging_memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *entry =
            system_paging_memmap_request.response->entries[i];

        struct System_Physical_Allocator *current = NULL;

        printf("MEMMAP: %xU-%xU(%xU) - %s\n", entry->base, entry->base + entry->length, entry->length, system_paging_memmap_string[entry->type]);

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length <= SYSTEM_PAGE_SIZE) {
                printf("MEMMAP:  -> skipping to small memory area\n");
                continue;
            }

            Int required_length = system_pa_calculate_required_size((Int)entry->length / SYSTEM_PAGE_SIZE);
            Int required_pages  = required_length / SYSTEM_PAGE_SIZE + 1;

            kassert(entry->length >= (U64)required_length); // TODO(robin): handle really small pages

            current = system_pa_init(
                    (void *)(Uintptr)(entry->base + hhdm_offset),
                    (Uintptr)entry->base,
                    required_pages,
                    (Int)(entry->length / SYSTEM_PAGE_SIZE));

            current->next      = &system_paging.pa_sentinel;
            current->next_16mb = &system_paging.pa_sentinel;
            current->next_4gb  = &system_paging.pa_sentinel;

            SL_APPEND_BASE(last, next, current);

            if (entry->base < MB(16) && entry->base + entry->length < MB(16)) {
                SL_APPEND_BASE(last_16mb, next_16mb, current);
            } else if (system_paging_pa_is_sentinel(first_non_16mb)) {
                first_non_16mb = current;
            }

            if (entry->base < GB(4) && entry->base + entry->length < GB(4)) {
                SL_APPEND_BASE(last_4gb, next_4gb, current);
            }


            printf("MEMMAP:  -> initialized physical allocator with size %i\n", current->size);
        }

        if (entry->base < MB(16) && entry->base + entry->length < MB(16)) {
            printf("MEMMAP:  -> 16 MB Zone\n");
        }

        if (entry->base < GB(4) && entry->base + entry->length < GB(4)) {
            printf("MEMMAP:  -> 4 GB Zone\n");
        }

        if (entry->base <= system_paging_exe_addr_request.response->physical_base
                && system_paging_exe_addr_request.response->physical_base < entry->base + entry->length) {

            printf("MEMMAP:  -> executable located here %xU -> %xU\n",
                    system_paging_exe_addr_request.response->physical_base,
                    system_paging_exe_addr_request.response->virtual_base);
        }

        if (entry->base <= physical_stack_top
                && physical_stack_top < entry->base + entry->length) {

            printf("MEMMAP:  -> stack located here %xU -> %xU\n",
                    physical_stack_top,
                    stack_top);
        }
    }

    system_paging.first      = base.next;
    system_paging.first_16mb = base.next_16mb;
    system_paging.first_4gb  = base.next_4gb;

    system_paging.last      = last;
    system_paging.last_16mb = last_16mb;
    system_paging.last_4gb  = last_4gb;

    system_paging.first_non_16mb = first_non_16mb;

    printf("PAGING: replacing page table\n");
    System_Paging_Address table = system_paging_table_clone(
        (void *)(
            (asm_cr3_read() & SYSTEM_PAGING_AMD64_FLAG_ADDRESS)
        + hhdm_offset)
    );
    asm_cr3_write((U64)table.physical);
    printf("PAGING: replaced page table\n");
}

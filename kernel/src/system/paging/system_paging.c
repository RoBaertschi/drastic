#include "system_physical_allocator.c"

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

internal void system_paging_setup(U64 stack_top) {
    kassert(system_paging_memmap_request.response != NULL);

    struct System_Physical_Allocator base = { 0 };

    // The sentinel always points to itself
    system_paging.pa_sentinel.next      = &system_paging.pa_sentinel;
    system_paging.pa_sentinel.next_16mb = &system_paging.pa_sentinel;
    system_paging.pa_sentinel.next_4gb  = &system_paging.pa_sentinel;

    struct System_Physical_Allocator *first      = &base;
    struct System_Physical_Allocator *first_16mb = &base;
    struct System_Physical_Allocator *first_4gb  = &base;

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

            last->next = current;
            last       = current;

            printf("MEMMAP:  -> initialized physical allocator with size %i\n", current->size);
        }

        if (entry->base < MB(16) && entry->base + entry->length < MB(16)) {
            printf("MEMMAP:  -> 16 MB Zone\n");
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                last_16mb->next_16mb = current;
                last_16mb            = current;
            }
        } else if (system_paging_pa_is_sentinel(first_non_16mb)
                && entry->type == LIMINE_MEMMAP_USABLE) {
            first_non_16mb = current;
        }

        if (entry->base < GB(4) && entry->base + entry->length < GB(4)) {
            printf("MEMMAP:  -> 4 GB Zone\n");
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                last_4gb->next_4gb = current;
                last_4gb           = current;
            }
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

    last      = &system_paging.pa_sentinel;
    last_16mb = &system_paging.pa_sentinel;
    last_4gb  = &system_paging.pa_sentinel;

    system_paging.first      = base->next;
    system_paging.first_16mb = base->next_16mb;
    system_paging.first_4gb  = base->next_4gb;

    system_paging.last      = base.last;
    system_paging.last_16mb = base.last_16mb;
    system_paging.last_4gb  = base.last_4gb;

    system_paging.first_non_16mb = first_non_16mb;
}

internal Uintptr system_paging_physical_alloc(System_Physical_Page_Kind kind) {
    // TODO(robin): cache current physical allocator used for better performance

    switch (kind) {
    case SYSTEM_PHYSICAL_PAGE_NORMAL:
        System_Physical_Allocator *current = system_paging.first_non_16mb;
    case SYSTEM_PHYSICAL_PAGE_4GB:
    case SYSTEM_PHYSICAL_PAGE_16MB:
    }
}

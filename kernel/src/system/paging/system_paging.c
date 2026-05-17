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

internal void system_paging_setup(U64 stack_top) {
    kassert(system_paging_memmap_request.response != NULL);

    System_Physical_Allocator sentinel = {0};

    struct System_Physical_Allocator *first      = &sentinel;
    struct System_Physical_Allocator *first_16mb = &sentinel;
    struct System_Physical_Allocator *first_4gb  = &sentinel;

    struct System_Physical_Allocator *last      = &sentinel;
    struct System_Physical_Allocator *last_16mb = &sentinel;
    struct System_Physical_Allocator *last_4gb  = &sentinel;

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
}

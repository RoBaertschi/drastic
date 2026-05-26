LIMINE_REQUEST
static volatile struct limine_hhdm_request system_hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

#define SYSTEM_PAGE_SIZE 4096

#include "system_gdt.c"
#include "system_idt.c"
#include "system_acpi.c"
#include "system_apic.c"
#include "system_physical_allocator.c"
#include "system_paging.c"
#include "system_vmm.c"

internal void system_setup(U64 stack_top) {
    asm_disable_interrupts();
    system_gdt_setup();
    system_idt_setup();
    asm_enable_interrupts();

    system_paging_setup(stack_top);

    system_acpi_setup();
    system_apic_setup();
}

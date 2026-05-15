LIMINE_REQUEST
static volatile struct limine_hhdm_request system_hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};


#include "system_gdt.c"
#include "system_idt.c"
#include "system_acpi.c"

internal void system_setup(void) {
    asm_disable_interrupts();
    system_gdt_setup();
    system_idt_setup();
    asm_enable_interrupts();
    system_acpi_setup();
}

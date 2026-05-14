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

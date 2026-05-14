#include "system_gdt.c"
#include "system_idt.c"

internal void system_setup(void) {
    system_gdt_setup();
}

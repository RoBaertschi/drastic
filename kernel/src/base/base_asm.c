internal void asm_disable_interrupts(void) {
    asm volatile("cli");
}

internal void asm_enable_interrupts(void) {
    asm volatile("sti");
}

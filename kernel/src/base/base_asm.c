internal void asm_disable_interrupts(void) {
    asm volatile("cli");
}

internal void asm_enable_interrupts(void) {
    asm volatile("sti");
}

internal U8 asm_port_inb(U16 port) {
    U8 out = 0;

    asm volatile(
        "inb %1, %0"
            : "=a"(out)
            : "d" (port)
    );

    return out;
}

internal void asm_port_outb(U16 port, U8 data) {
    asm volatile (
        "outb %1, %0"
        :
        : "d"(port), "a"(data)
    );
}

#define ASM_MSR_APIC_BASE 0x1B

internal U64 asm_read_msr(U32 msr) {
    U32 high_edx = 0;
    U32 low_eax  = 0;

    asm volatile (
        "rdmsr"
        : "=d"(high_edx), "=a"(low_eax)
        : "c"(msr)
    );

    return (U64)low_eax | ((U64)high_edx << 32);
}

internal void asm_invlpg(void *ptr) {
    asm volatile ("invlpg (%0)" :: "r"(ptr) : "memory");
}

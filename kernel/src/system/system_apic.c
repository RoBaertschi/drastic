#define SYSTEM_APIC_PIC_COMMAND_MASTER 0x20
#define SYSTEM_APIC_PIC_DATA_MASTER 0x21

#define SYSTEM_APIC_PIC_COMMAND_SLAVE 0xA0
#define SYSTEM_APIC_PIC_DATA_SLAVE 0xA1

#define SYSTEM_APIC_PIC_ICW_1 0x11
#define SYSTEM_APIC_PIC_ICW_2_M 0x20
#define SYSTEM_APIC_PIC_ICW_2_S 0x28
#define SYSTEM_APIC_PIC_ICW_3_M 0x4
#define SYSTEM_APIC_PIC_ICW_3_S 0x2
#define SYSTEM_APIC_PIC_ICW_4 0x1

internal void system_apic_pic_disable(void) {
    asm_port_outb(SYSTEM_APIC_PIC_COMMAND_MASTER, SYSTEM_APIC_PIC_ICW_1);
    asm_port_outb(SYSTEM_APIC_PIC_COMMAND_SLAVE,  SYSTEM_APIC_PIC_ICW_1);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_MASTER,    SYSTEM_APIC_PIC_ICW_2_M);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_SLAVE,     SYSTEM_APIC_PIC_ICW_2_S);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_MASTER,    SYSTEM_APIC_PIC_ICW_3_M);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_SLAVE,     SYSTEM_APIC_PIC_ICW_3_S);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_MASTER,    SYSTEM_APIC_PIC_ICW_4);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_SLAVE,     SYSTEM_APIC_PIC_ICW_4);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_MASTER,    0xFF);
    asm_port_outb(SYSTEM_APIC_PIC_DATA_SLAVE,     0xFF);

    printf("APIC: disabled pic\n");
}

#define SYSTEM_APIC_SPV_OFFSET 0xF0
#define SYSTEM_APIC_EOI_OFFSET 0xB0
#define SYSTEM_APIC_TIMER_LVT_OFFSET 0x320
#define SYSTEM_APIC_LOCAL_APIC_ID_OFFSET 0x20

internal void system_apic_setup(void) {
    system_apic_pic_disable();

    U64 apic_base = asm_read_msr(ASM_MSR_APIC_BASE);
    if ((1 << 8) & apic_base) {
        printf("APIC: this is the bootstrap processor\n");
    }

    if ((1 << 11) & apic_base) {
        printf("APIC: apic is enabled\n");
    } else {
        kpanic(STR("APIC: apic is disabled"));
    }

    U64 base_address_mask = (U64)(((U64)1 << 36) - 1) & ~(U64)(((U64)1 << 12) - 1);
    U64 base_address = apic_base & base_address_mask;

    volatile U8 *address = (U8*)(base_address + system_hhdm_request.response->offset);

    printf("APIC: base address = %xU\n", base_address);
    printf("APIC: virtual address = %p\n", address);

    // volatile U32 *spv = (U32*)(address + 0xF0);
    // *spv = (1 << 8) | 0xFF;

    // TODO(robin): implement paging to map the local apic
    // printf("APIC: id=%xU\n", (U64)*(U32 volatile*)(address + SYSTEM_APIC_LOCAL_APIC_ID_OFFSET));
}

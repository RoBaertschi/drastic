#define SYSTEM_IDT_ENTRIES 256
#define SYSTEM_IDT_ENTRY_SIZE 16

typedef struct System_Idt_Entry {
    U64 offset;
    U16 segment_selector;
    U8  ist;
    U8  flags;
} System_Idt_Entry;

typedef struct System_Idt {
    U8 data[SYSTEM_IDT_ENTRIES * SYSTEM_IDT_ENTRY_SIZE];
} System_Idt;

global System_Idt system_idt;

typedef struct PACKED System_Idt_Register {
    U16 limit; // total size minus 1
    U64 base;
} System_Idt_Register;

USED
global U8 _system_idt_register_check_correct_size[size_of(System_Idt_Register) == 10 ? 1 : -1];

internal void system_idt_entry_encode(U8 *target, System_Idt_Entry entry) {
    U16 *target_u16 = (U16 *)target;

    target_u16[0] = (U16) entry.offset        & 0xFFFF;
    target_u16[3] = (U16)(entry.offset >> 16) & 0xFFFF;
    target_u16[4] = (U16)(entry.offset >> 32) & 0xFFFF;
    target_u16[5] = (U16)(entry.offset >> 48) & 0xFFFF;

    target_u16[1] = entry.segment_selector;

    target[4] = entry.ist & 0x7;

    target[5] = entry.flags;

    target_u16[6] = 0;
    target_u16[7] = 0;
}

internal void system_idt_interrupt_entry(U8 vector,
                                         void *handler,
                                         U8 dpl,
                                         U8 ist) {

    system_idt_entry_encode(
        &system_idt.data[(Int)vector * SYSTEM_IDT_ENTRY_SIZE],
        (System_Idt_Entry) {
            .offset           = (U64)handler,
            .segment_selector = SYSTEM_GDT_KCODE_SELECTOR,
            .ist              = ist,
            .flags            = 0b1110 | ((dpl & 0b11) << 5) | (1 << 7),
        });
}

#define SYSTEM_IDT_INTERRUPT_VECTOR_NAMES 22

String system_idt_interrupt_vector_short_name[SYSTEM_IDT_INTERRUPT_VECTOR_NAMES] = {
    STR("#DE"),
    STR("#DB"),
    STR("NMI"),
    STR("#BP"),
    STR("#OF"),
    STR("#BR"),
    STR("#UD"),
    STR("#NM"),
    STR("#DF"),
    STR("N/A"),
    STR("#TS"),
    STR("#NP"),
    STR("#SS"),
    STR("#GP"),
    STR("#PF"),
    STR("N/A"),
    STR("#MF"),
    STR("#AC"),
    STR("#MC"),
    STR("#XM"),
    STR("#VE"),
    STR("#CP")
};

String system_idt_interrupt_vector_name[SYSTEM_IDT_INTERRUPT_VECTOR_NAMES] = {
    STR("Divide Error"),
    STR("Debug Exception"),
    STR("NMI Interrupt"),
    STR("Breakpoint"),
    STR("Overflow"),
    STR("BOUND Range Exceeded"),
    STR("Invalid Opcode (Undefined Opcode)"),
    STR("Device Not Available (No Math Coprocessor)"),
    STR("Double Fault"),
    STR("Coprocessor Segment Overrun (reserved)"),
    STR("Invalid TSS"),
    STR("Segment Not Present"),
    STR("Stack-Segment Fault"),
    STR("General Protection"),
    STR("Page Fault"),
    STR("Intel reserved. Do not use."),
    STR("x87 FPU Floating-Point Error (Math Fault)"),
    STR("Alignment Check"),
    STR("Machine Check"),
    STR("SIMD Floating-Point Exception"),
    STR("Virtualization Exception"),
    STR("Control Protection Exception"),
};

typedef struct System_Idt_Interrupt_Stack_Frame {
    U64 r15;
    U64 r14;
    U64 r13;
    U64 r12;
    U64 r11;
    U64 r10;
    U64 r9;
    U64 r8;
    U64 rbp;
    U64 rdi;
    U64 rsi;
    U64 rdx;
    U64 rcx;
    U64 rbx;
    U64 rax;

    U64 vector;
    U64 error_code;

    U64 rip;
    U64 cs;
    U64 rflags;
    U64 rsp;
    U64 ss;
} System_Idt_Interrupt_Stack_Frame;

void system_idt_interrupt_handler(System_Idt_Interrupt_Stack_Frame *stack_frame) {
    if (stack_frame->vector < SYSTEM_IDT_INTERRUPT_VECTOR_NAMES) {
        printf("interrupt %s(%s) (error: %U):\n",
               system_idt_interrupt_vector_short_name[stack_frame->vector],
               system_idt_interrupt_vector_name[stack_frame->vector],
               stack_frame->error_code);
    } else {
        printf("interrupt (vector %U, error: %U):\n",
               stack_frame->vector,
               stack_frame->error_code);
    }
    printf("r15: %xU\n", stack_frame->r15);
    printf("r14: %xU\n", stack_frame->r14);
    printf("r13: %xU\n", stack_frame->r13);
    printf("r12: %xU\n", stack_frame->r12);
    printf("r11: %xU\n", stack_frame->r11);
    printf("r10: %xU\n", stack_frame->r10);
    printf("r9:  %xU\n", stack_frame->r9);
    printf("r8:  %xU\n", stack_frame->r8);
    printf("rbp: %xU\n", stack_frame->rbp);
    printf("rdi: %xU\n", stack_frame->rdi);
    printf("rsi: %xU\n", stack_frame->rsi);
    printf("rdx: %xU\n", stack_frame->rdx);
    printf("rcx: %xU\n", stack_frame->rcx);
    printf("rbx: %xU\n", stack_frame->rbx);
    printf("rax: %xU\n", stack_frame->rax);

    printf("\n");

    printf("rip:    %xU\n",    stack_frame->rip);
    printf("cs:     %xU\n",     stack_frame->cs);
    printf("rflags: %xU\n", stack_frame->rflags);

    if ((stack_frame->cs & 0x3) != 0) {
        printf("rsp:    %xU\n",     stack_frame->rsp);
        printf("ss:     %xU\n", stack_frame->ss);
    }

    kpanic(STR("interrupt handler called"));
}

internal void system_idt_load(U16 limit, U64 base) {
    System_Idt_Register idtr = { limit, base };
    asm volatile(
        "lidt %0"
        :
        : "m"(idtr)
    );
}

extern U8 system_idt_isr_stub_0[];

internal void system_idt_setup(void) {
    for (Int i = 0; i < (Int)SYSTEM_IDT_ENTRIES; i++) {
        system_idt_interrupt_entry(
            (U8)i,
            system_idt_isr_stub_0 + (i * 16),
            0,
            0);
    }

    system_idt_load((U16)size_of(system_idt.data) - 1,
                    (U64)(Uintptr)system_idt.data);
}

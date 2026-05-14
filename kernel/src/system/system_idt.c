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

typedef struct PACKED System_Idt_Register {
    U16 limit; // total size minus 1
    U64 base;
} System_Idt_Register;

internal void system_idt_entry_encode(U8 *target, System_Idt_Entry entry) {
    U16 *target_u16 = (U16 *)target;

    target_u16[0] = (U16) entry.offset        & 0xFFFF;
    target_u16[3] = (U16)(entry.offset >> 16) & 0xFFFF;
    target_u16[4] = (U16)(entry.offset >> 32) & 0xFFFF;
    target_u16[5] = (U16)(entry.offset >> 48) & 0xFFFF;

    target_u16[1] = entry.segment_selector;

    target[4] = entry.ist & 0x7;

    target[5] = entry.flags;
}

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

void system_idt_interrupt_handler(System_Idt_Interrupt_Stack_Frame *stack_frame) {}

internal void system_idt_setup(void) {
    
}

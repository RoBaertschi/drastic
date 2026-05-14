#define SYSTEM_GDT_NULL_SEGMENT  0
#define SYSTEM_GDT_KCODE_SEGMENT 1
#define SYSTEM_GDT_KDATA_SEGMENT 2
#define SYSTEM_GDT_UDATA_SEGMENT 3
#define SYSTEM_GDT_UCODE_SEGMENT 4

// rpl = requested privilage level
#define SYSTEM_GDT_SEGMENT_SELECTOR(index, rpl) ((U64)(((index) << 3) | (rpl)))

#define SYSTEM_GDT_KDATA_SELECTOR SYSTEM_GDT_SEGMENT_SELECTOR(SYSTEM_GDT_KDATA_SEGMENT, 0)
#define SYSTEM_GDT_KCODE_SELECTOR SYSTEM_GDT_SEGMENT_SELECTOR(SYSTEM_GDT_KCODE_SEGMENT, 0)

#define SYSTEM_GDT_SEGMENT_COUNT 5

// This constant is dangerous and only true for data and code segment, system segments are 16 bytes
// #define SYSTEM_GDT_SEGMENT_SIZE  8 // bytes

#define SYSTEM_GDT_SEGFLAG_LONG_MODE BIT(1)
#define SYSTEM_GDT_SEGFLAG_DB BIT(2)
#define SYSTEM_GDT_SEGFLAG_GRANULARITY BIT(3)

typedef struct System_Gdt_Entry {
    U64     base;
    U32     limit;       // NOTE: only 20-bits
    U8      access_byte;
    U8      flags;       // NOTE: only 4-bits
} System_Gdt_Entry;

typedef struct System_Gdt {
    System_Gdt_Entry entries[SYSTEM_GDT_SEGMENT_COUNT];
    U8               data   [SYSTEM_GDT_SEGMENT_COUNT * 8];
} System_Gdt;

global System_Gdt system_gdt = {
    .entries = {
        [SYSTEM_GDT_KCODE_SEGMENT] = {
            .base        = 0,
            .limit       = 0xFFFFF,
            .access_byte = 0x9A,
            .flags       = SYSTEM_GDT_SEGFLAG_LONG_MODE |
                           SYSTEM_GDT_SEGFLAG_GRANULARITY,
        },
        [SYSTEM_GDT_KDATA_SEGMENT] = {
            .base        = 0,
            .limit       = 0xFFFFF,
            .access_byte = 0x92,
            .flags       = SYSTEM_GDT_SEGFLAG_DB |
                           SYSTEM_GDT_SEGFLAG_GRANULARITY,
        },
        [SYSTEM_GDT_UDATA_SEGMENT] = {
            .base        = 0,
            .limit       = 0xFFFFF,
            .access_byte = 0xF2,
            .flags       = SYSTEM_GDT_SEGFLAG_DB |
                           SYSTEM_GDT_SEGFLAG_GRANULARITY,
        },
        [SYSTEM_GDT_UCODE_SEGMENT] = {
            .base        = 0,
            .limit       = 0xFFFFF,
            .access_byte = 0xFA,
            .flags       = SYSTEM_GDT_SEGFLAG_LONG_MODE |
                           SYSTEM_GDT_SEGFLAG_GRANULARITY,
        },
    }
};

internal void system_gdt_entry_encode(U8 *target, System_Gdt_Entry entry) {
    if (entry.limit > 0xFFFFF) {
        kpanic(STR("GDT cannot encode limits larger than 0xFFFFF"));
    }

    target[0] =  entry.limit        & 0xFF;
    target[1] = (entry.limit >> 8)  & 0xFF;
    target[6] = (entry.limit >> 16) & 0x0F;

    target[2] =  entry.base        & 0xFF;
    target[3] = (entry.base >> 8)  & 0xFF;
    target[4] = (entry.base >> 16) & 0xFF;
    target[7] = (entry.base >> 24) & 0xFF;

    target[5] = entry.access_byte;
    target[6] = (entry.flags << 4);
}

typedef struct PACKED System_Gdt_Register {
    U16 limit; // total size minus 1
    U64 base;
} System_Gdt_Register;

USED
global U8 _system_gdt_register_check_correct_size[size_of(System_Gdt_Register) == 10 ? 1 : -1];

internal void system_gdt_load(U16 limit, U64 base) {
    System_Gdt_Register gdtr = { limit, base };
    asm volatile(
            "lgdt %0"
            :
            : "m"(gdtr)
            : "memory"
    );

    // reload segment registers
    asm volatile(
            "movw %0, %%ax\n\t"
            "movw %%ax, %%ds\n\t"
            "movw %%ax, %%es\n\t"
            "movw %%ax, %%ss\n\t"
            "xor %%rax, %%rax\n\t"
            "movw %%ax, %%fs\n\t"
            "movw %%ax, %%gs\n\t"
            "pushq %1\n\t"
            "leaq 1f(%%rip), %%rax\n\t"
            "pushq %%rax\n\t"
            "lretq\n\t"
            "1:\n\t"
            :
            : "i"(SYSTEM_GDT_SEGMENT_SELECTOR(SYSTEM_GDT_KDATA_SEGMENT, 0)), "i"(SYSTEM_GDT_SEGMENT_SELECTOR(SYSTEM_GDT_KCODE_SEGMENT, 0))
            : "rax", "memory"
    );
}


internal void system_gdt_setup(void) {
    for (Int i = 0; i < SYSTEM_GDT_SEGMENT_COUNT; i++) {
        system_gdt_entry_encode(&system_gdt.data[i * 8], system_gdt.entries[i]);
    }

    system_gdt_load((U16)size_of(system_gdt.data) - 1, (Uintptr)&system_gdt.data);
}

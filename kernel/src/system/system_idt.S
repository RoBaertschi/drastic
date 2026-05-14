.extern system_idt_interrupt_handler

system_idt_interrupt_stub:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    /*
        C ABI expects direction flag clear.
        iretq will restore the old RFLAGS later.
    */
    cld

    movq %rsp, %rdi

    // align stack
    movq %rsp, %rax
    andq $-16, %rsp
    subq $8, %rsp
    pushq %rax

    call system_idt_interrupt_handler

    popq %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    add $16, %rsp
    iretq

.macro ISR_NO_ERR vector
.global system_idt_isr_stub_\vector
system_idt_isr_stub_\vector:
    pushq 0
    pushq \vector
    jmp system_idt_interrupt_stub
.endm

.macro ISR_ERR vector
.global system_idt_isr_stub_\vector
system_idt_isr_stub_\vector:
    pushq \vector
    jmp system_idt_interrupt_stub
.endm

ISR_NO_ERR 0      /* #DE Divide Error */
ISR_NO_ERR 1      /* #DB Debug */
ISR_NO_ERR 2      /* NMI */
ISR_NO_ERR 3      /* #BP Breakpoint */
ISR_NO_ERR 4      /* #OF Overflow */
ISR_NO_ERR 5      /* #BR Bound Range Exceeded */
ISR_NO_ERR 6      /* #UD Invalid Opcode */
ISR_NO_ERR 7      /* #NM Device Not Available */

ISR_ERR   8      /* #DF Double Fault */

ISR_NO_ERR 9      /* legacy Coprocessor Segment Overrun */

ISR_ERR   10     /* #TS Invalid TSS */
ISR_ERR   11     /* #NP Segment Not Present */
ISR_ERR   12     /* #SS Stack Segment Fault */
ISR_ERR   13     /* #GP General Protection Fault */
ISR_ERR   14     /* #PF Page Fault */

ISR_NO_ERR 15     /* reserved */
ISR_NO_ERR 16     /* #MF x87 Floating-Point Exception */

ISR_ERR   17     /* #AC Alignment Check */

ISR_NO_ERR 18     /* #MC Machine Check */
ISR_NO_ERR 19     /* #XM/#XF SIMD Floating-Point Exception */
ISR_NO_ERR 20     /* #VE Virtualization Exception */

ISR_ERR   21     /* #CP Control Protection Exception */

ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28     /* #HV Hypervisor Injection Exception */

ISR_ERR   29     /* #VC VMM Communication Exception */
ISR_ERR   30     /* #SX Security Exception */

ISR_NO_ERR 31

.section .note.GNU-stack,"",@progbits

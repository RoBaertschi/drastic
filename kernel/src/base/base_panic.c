// Halt and catch fire function.
NO_RETURN
static void hcf(void) {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}


global Stream _panic_output_stream = STREAM_NO_OP;

internal NO_RETURN void kpanic(String panic_message) {
    stream_write_string(_panic_output_stream, STR("KPANIC: "));
    stream_write_string(_panic_output_stream, panic_message);
    stream_write_string(_panic_output_stream, STR("\n"));

    hcf();
}

internal void kpanic_set_output_stream(Stream s) {
    _panic_output_stream = s;
}

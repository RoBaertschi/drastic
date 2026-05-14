#define SERIAL_COM1 ((U16)0x3F8)

#define SERIAL_PO_RECEIVE_BUFFER  0 // read
#define SERIAL_PO_TRANSMIT_BUFFER 0 // write
#define SERIAL_PO_INTERRUPT_ENABLE_REGISTER 1

#define SERIAL_PO_LEAST_DIVISOR 0
#define SERIAL_PO_MOST_DIVISOR  1

#define SERIAL_PO_INTERRUPT_IDENTIFICATION 2 // read
#define SERIAL_PO_FIFO_CONTROL_REGISTER    2 // write

#define SERIAL_PO_LINE_CONTROL_REGISTER  3
#define SERIAL_PO_MODEM_CONTROL_REGISTER 4
#define SERIAL_PO_LINE_STATUS_REGISTER   5 // read
#define SERIAL_PO_MODEM_STATUS_REGISTER  6 // read
#define SERIAL_PO_SCRATCH_REGISTER       7


internal U8 serial_inb(U16 port) {
    U8 out = 0;

    asm volatile(
        "inb %1, %0"
            : "=a"(out)
            : "d" (port)
    );

    return out;
}

internal void serial_outb(U16 port, U8 data) {
    asm volatile (
        "outb %1, %0"
        :
        : "d"(port), "a"(data)
    );
}

internal bool serial_init(U16 port) {
    serial_outb(port + SERIAL_PO_INTERRUPT_ENABLE_REGISTER, 0x00); // disable interrupts
    serial_outb(port + SERIAL_PO_LINE_CONTROL_REGISTER,     0x80); // enable DLAB to set baud divisor
    serial_outb(port + SERIAL_PO_LEAST_DIVISOR,             0x03); // set divisor to 3, 38400 baud
    serial_outb(port + SERIAL_PO_MOST_DIVISOR,              0x00); // high byte
    serial_outb(port + SERIAL_PO_LINE_CONTROL_REGISTER,     0x03); // 8 bits, no parity, one stop bit
    serial_outb(port + SERIAL_PO_FIFO_CONTROL_REGISTER,     0xC7); // enable & clear fifo, 14-byte threshold
    serial_outb(port + SERIAL_PO_MODEM_CONTROL_REGISTER,    0x0B); // IRQs enabled, RTS/DSR set

    // test serial
    serial_outb(port + SERIAL_PO_MODEM_CONTROL_REGISTER, 0x1E); // enable loopback
    serial_outb(port + SERIAL_PO_TRANSMIT_BUFFER,        0xAE); // enable loopback

    if (serial_inb(port + SERIAL_PO_RECEIVE_BUFFER) != 0xAE) {
        return false;
    }

    serial_outb(port + SERIAL_PO_MODEM_CONTROL_REGISTER, 0x0F); // IRQs enabled, out 1 & 2 enabled
    return true;
}

internal bool serial_read_available(U16 port) {
    return !!(serial_inb(port + SERIAL_PO_LINE_STATUS_REGISTER) & 0x01);
}

internal bool serial_write_available(U16 port) {
    return !!(serial_inb(port + SERIAL_PO_LINE_STATUS_REGISTER) & 0x20);
}

internal U8 serial_read_blocking(U16 port) {
    while (!serial_read_available(port));

    return serial_inb(port);
}

internal void serial_write_blocking(U16 port, U8 data) {
    while (!serial_write_available(port));

    serial_outb(port, data);
}

internal void serial_write_string(U16 port, String str) {
    for (Int i = 0; i < str.len; i++) {
        serial_write_blocking(port, string_get(str, i));
    }
}

internal void serial_write_bytes(U16 port, Bytes buffer) {
    for (Int i = 0; i < buffer.len; i++) {
        serial_write_blocking(port, bytes_get(buffer, i));
    }
}

internal STREAM_FUNCTION(serial_stream_func) {
    U16 port = (U16)(Uintptr)data;

    switch (op) {
    case STREAM_OP_QUERY:
        return STREAM_CAP_WRITE | STREAM_CAP_READ;
    case STREAM_OP_WRITE:
        serial_write_bytes(port, buffer);
        return buffer.len;
    case STREAM_OP_READ:
        Int n = 0;
        while(!serial_read_available(port));

        while (serial_read_available(port) && n < buffer.len) {
            bytes_set(buffer, n, serial_inb(port));
            n             += 1;
        }
        return n;
    }

    // TODO(robin): return unsupported error
    return 0;
}

internal Stream serial_stream_make(U16 port) {
    return (Stream){
        .data = (void *)(Uintptr)port,
        .func = serial_stream_func,
    };
}

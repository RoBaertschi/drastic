internal U8 format_digit(U64 value) {
    kassert(value < 16);

    if (value < 10) {
        return '0' + (U8)value;
    }

    return 'A' + ((U8)value - 10);
}

internal void format_number(Stream output, U64 value, bool is_signed, U8 base) {
    U8 buffer_data[25] = {0};
    Bytes buffer       = { .ptr = buffer_data, .len = size_of(buffer_data) };
    Int i              = 0;
    base               = !base ? 10 : base;

    do {
        bytes_set(buffer, i, format_digit(value % base));
        i     += 1;
        value /= base;
    } while(value > 0);

    if (base == 16) {
        Int left_zeros = 16 - i;
        for (Int j = 0; j < left_zeros; j++) {
            bytes_set(buffer, i, '0');
            i += 1;
        }
    }

    if (is_signed) {
        bytes_set(buffer, i, '-');
        i += 1;
    }

    Bytes output_buffer = bytes_slice(buffer, 0, i);
    bytes_reverse(output_buffer);

    switch (base) {
    case 2:
        stream_write_string(output, STR("0b"));
        break;
    case 8:
        stream_write_string(output, STR("0o"));
        break;
    case 16:
        stream_write_string(output, STR("0x"));
        break;
    }
    stream_write(output, output_buffer);
}

internal void format_number_i64(Stream output, I64 value, U8 base) {
    bool is_signed = false;
    if (value < 0) {
        value      = -value;
        is_signed  = true;
    }
    format_number(output, (U64)value, is_signed, base);
}

internal void format_number_u64(Stream output, U64 value, U8 base) {
    format_number(output, value, false, base);
}

internal void vformat(Stream output, String format, va_list args) {
    for (Int i = 0; i < format.len; i++) {
        U8 ch = string_get(format, i);
        if (ch != '%') {
            stream_write_byte(output, ch);
            continue;
        }

        i += 1;

        if (i >= format.len) {
            stream_write_string(output,
                    STR("%(<out of bounds>)"));
            continue;
        }

        ch = string_get(format, i);

        U8 base = 0;

        if (ch == 'x') {
            base = 16;
            i += 1;
            if (i >= format.len) {
                stream_write_string(output,
                        STR("%x(<out of bounds>)"));
                continue;
            }

            ch = string_get(format, i);
        }

        switch (ch) {
        case 'p': {
            void *ptr = va_arg(args, void*);
            format_number(output, (U64)(Uintptr)ptr, false, !base ? 16 : base);
            break;
        }
        case 'i': {
            Int integer = va_arg(args, Int);
            format_number_i64(output, (I64)integer, base);
            break;
        }
        case 'u': {
            Uint uinteger = va_arg(args, Uint);
            format_number_u64(output, (U64)uinteger, base);
            break;
        }
        case 'I': {
            I64 i64 = va_arg(args, I64);
            format_number_i64(output, i64, base);
            break;
        }
        case 'U': {
            U64 u64 = va_arg(args, U64);
            format_number_u64(output, u64, base);
            break;
        }
        case 's': {
            String s = va_arg(args, String);
            stream_write_string(output, s);
            break;
        }
        case 'S': {
            char const *cstr = va_arg(args, char const*);
            String s         = string_from_char(cstr);
            stream_write_string(output, s);
            break;
        }
        default: {
            stream_write_string(output,
                    STR("%(<unsupported format specifier>)"));
            return; // It is not really a good idea to continue from here on
        }
        }
    }
}

internal void format(Stream output, String format, ...) {
    va_list args;
    va_start(args, format);
    vformat(output, format, args);
    va_end(args);
}

global Stream _format_output_stream = STREAM_NO_OP;

internal void format_set_output_stream(Stream s) {
    _format_output_stream = s;
}

internal void printf(char const *format, ...) {
    String format_string = string_from_char(format);

    va_list args;
    va_start(args, format);
    vformat(_format_output_stream, format_string, args);
    va_end(args);
}

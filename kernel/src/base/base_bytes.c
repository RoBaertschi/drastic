typedef struct Bytes {
    U8  *ptr;
    Int len;
} Bytes;

#define BYTES_FROM_PTR(ptr_) ((Bytes) { .ptr = (U8*)ptr_, .len = size_of(*(ptr_)) })

internal U8 bytes_get(Bytes b, Int i) {
    kassert(0 <= i && i < b.len);

    return b.ptr[i];
}

internal void bytes_set(Bytes b, Int i, U8 byte) {
    kassert(0 <= i && i < b.len);

    b.ptr[i] = byte;
}

internal Bytes bytes_slice(Bytes b, Int from, Int to) {
    kassert(0 <= from && from <= to && to <= b.len);

    Bytes bytes = { .len = to - from };
    bytes.ptr   = b.ptr + from;
    return bytes;
}

internal void bytes_reverse(Bytes b) {
    if (b.len <= 1) {
        // already reversed
        return;
    }

    U8 *start = b.ptr;
    U8 *end   = b.ptr + b.len - 1;
    U8 temp   = 0;

    while (end > start) {
        temp   = *start;
        *start = *end;
        *end   = temp;

        start += 1;
        end   -= 1;
    }
}

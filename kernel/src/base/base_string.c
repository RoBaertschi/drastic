typedef struct String {
    U8 const  *ptr;
    Int       len;
} String;

#define STR(s) ((String) { .ptr = (U8 const *)(s), .len = sizeof(s)-1 })

internal String string_from_char(char const *s) {
    Int len = 0;
    if (s) {
        for (char const *str = s; *str; str++) {
            len += 1;
        }
    }

    return (String) {
        .ptr = (U8 const *)s,
        .len = len,
    };
}

internal U8 string_get(String s, Int i) {
    kassert(0 <= i && i < s.len);

    return s.ptr[i];
}

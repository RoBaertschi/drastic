typedef struct String {
    U8 const  *ptr;
    Int       len;
} String;

#define STR(s) ((String) { .ptr = (U8 const *)(s), .len = sizeof(s)-1 })

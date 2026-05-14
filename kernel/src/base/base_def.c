typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t  I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;

typedef float  F32;
typedef double F64;

typedef ptrdiff_t Int;
typedef size_t    Uint;

typedef uintptr_t Uintptr;
typedef intptr_t  Intptr;


#define internal static
#define global   static

#define BIT(bit) (1 << (bit))

#define NO_RETURN __attribute__ ((noreturn))
#define SECTION(s) __attribute__((section(s)))
#define USED __attribute__((used))
#define PACKED __attribute__((packed))
#define COLD __attribute__((cold))
#define LIMINE_REQUEST USED SECTION(".limine_requests")

#define size_of(T) ((Int)sizeof(T))

#define count_of(a) (size_of(a) / size_of(a[0]))

#define kassert(condition) kassert_message((condition), #condition)
internal void kassert_message(bool assertion, char const *message);

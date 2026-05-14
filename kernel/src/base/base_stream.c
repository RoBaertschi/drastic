// TODO(robin): error handling

typedef enum Stream_Capabilies {
    STREAM_CAP_WRITE = BIT(0),
    STREAM_CAP_READ  = BIT(1),
} Stream_Capabilies;

typedef enum Stream_Operation {
    // queries capabilities, returns a U8 as the capabilities bits
    // buffer is ignored
    // this operation must be supported by the interface
    STREAM_OP_QUERY,
    // write data to stream from buffer
    // returns the amount of written data
    STREAM_OP_WRITE,
    // read data from stream into buffer
    // returns the amount of read data
    STREAM_OP_READ,
} Stream_Operation;

#define STREAM_FUNCTION(name) Int (name)(void *data, Stream_Operation op, Bytes buffer)

typedef STREAM_FUNCTION(Stream_Function);

typedef struct Stream {
    void            *data;
    Stream_Function *func;
} Stream;

internal STREAM_FUNCTION(stream_no_op_func) {
    (void)data;
    (void)op;
    (void)buffer;
    return 0;
}

#define STREAM_NO_OP (Stream){ .func = stream_no_op_func };

internal Int stream_write_string(Stream s, String str) {
    Bytes bytes = { .ptr = (U8*)str.ptr, .len = str.len };

    return s.func(s.data, STREAM_OP_WRITE, bytes);
}

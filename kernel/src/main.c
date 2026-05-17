#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limine.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "../cc-runtime/src/cc-runtime.c"
#pragma GCC diagnostic pop

#include "memory.c"

#include "base/base.c"
#include "serial/serial.c"
#include "system/system.c"

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

LIMINE_REQUEST
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

LIMINE_REQUEST
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

USED SECTION(".limine_requests_start")
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

USED SECTION(".limine_requests_end")
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    void *frame_ptr;
    asm volatile("mov %%rsp, %0" : "=r"(frame_ptr));

    if (!serial_init(SERIAL_COM1)) {
        // TODO(robin): support operation without serial
        hcf();
    }

    Stream serial_stream = serial_stream_make(SERIAL_COM1);
    kpanic_set_output_stream(serial_stream);
    format_set_output_stream(serial_stream);

    printf("\n\n\n");

    printf("frame_ptr = %p\n", frame_ptr);


    format(serial_stream, STR("Test %p ptr\n\nHi %i %u\n%s"),
           &serial_stream,
           (Int)-44,
           (Uint)55, STR("Test %p ptr\n\nHi %i %u\n"));

    printf("%p %S\n", &serial_stream, "HIIIIIII\n");

    serial_write_string(SERIAL_COM1, STR("Hello Kernel World!\n"));

    system_setup((U64)(Uintptr) frame_ptr);

    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Print a nice pattern to screen as an example.
    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    volatile uint32_t *fb_ptr = framebuffer->address;
    for (size_t y = 0; y < framebuffer->height; y++) {
        for (size_t x = 0; x < framebuffer->width; x++) {
            uint32_t nX = x * 255 / framebuffer->width;
            uint32_t nY = y * 255 / framebuffer->height;
            fb_ptr[y * (framebuffer->pitch / 4) + x] = (nY << 8) | nX;
        }
    }

    // Int volatile test = 1 / 0;

    for(;;);

    // We're done, just hang...
    hcf();
}

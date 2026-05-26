typedef enum Log_Level {
    LOGL_TRACE,
    LOGL_DEBUG,
    LOGL_INFO,
    LOGL_WARN,
    LOGL_ERROR,
    LOGL_FATAL,

    LOGL__COUNT,
} Log_Level;

global char const *log_level_strings[LOGL__COUNT] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL",
};

typedef enum Log_Subystem {
    LOGS_NONE,
    LOGS_GDT,
    LOGS_IDT,
    LOGS_ACPI,
    LOGS_APIC,
    LOGS_PAGING,

    LOGS__COUNT,
} Log_Subystem;

global char const *log_subsystem_strings[LOGS__COUNT] = {
    NULL,
    "GDT",
    "IDT",
    "ACPI",
    "APIC",
    "PAGING",
};

typedef struct Log_Position {
    Int        line;
    char const *file;
} Log_Position;

#define LOG_POSITION ((Log_Position){ .line = __LINE__, .file = __FILE__ })

#define LOG_FUNCTION(name) void (name)(void *data, Log_Level level, Log_Subystem subsystem, Log_Position pos, char const *format, va_list list)

typedef LOG_FUNCTION(*logger_func);

typedef struct Logger {
    logger_func func;
    void        *data;
} Logger;

void logger_log(
    Logger logger,
    Log_Level level,
    Log_Subystem subsystem,
    Log_Position pos,
    char const *format,
    ...) {
    va_list list;
    va_start(list, format);

    logger.func(logger.data, level, subsystem, pos, format, list);

    va_end(list);
}

void logger_vlog(
    Logger logger,
    Log_Level level,
    Log_Subystem subsystem,
    Log_Position pos,
    char const *format,
    va_list list) {

    logger.func(logger.data, level, subsystem, pos, format, list);
}


LOG_FUNCTION(logger_printf) {
    (void)data;
    printf(
        "%S: %S - %S:%i - ",
        log_subsystem_strings[subsystem],
        log_level_strings[level],
        pos.file,
        pos.line
    );

    vprintf(format, list);
    printf("\n");
}

#define LOGGER_DEFAULT ((Logger){ .func = logger_printf })

USED
global Logger _logger = LOGGER_DEFAULT;

#define LOG_TRACE(subsystem, format, ...) logger_log(_logger, LOGL_TRACE, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(subsystem, format, ...) logger_log(_logger, LOGL_DEBUG, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(subsystem, format, ...) logger_log(_logger, LOGL_INFO, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(subsystem, format, ...) logger_log(_logger, LOGL_WARN, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(subsystem, format, ...) logger_log(_logger, LOGL_ERROR, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(subsystem, format, ...) logger_log(_logger, LOGL_FATAL, (subsystem), LOG_POSITION, format __VA_OPT__(,) __VA_ARGS__)

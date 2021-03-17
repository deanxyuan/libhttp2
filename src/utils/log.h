#pragma once

#include <stdlib.h>

#ifdef __GNUC__
#define RAPTOR_LIKELY(x) __builtin_expect((x), 1)
#define RAPTOR_UNLIKELY(x) __builtin_expect((x), 0)
#define RAPTOR_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define RAPTOR_LIKELY(x) (x)
#define RAPTOR_UNLIKELY(x) (x)
#define RAPTOR_MUST_USE_RESULT
#endif

#define kDebugLevel 0
#define kInfoLevel 1
#define kWarnLevel 2
#define kErrorLevel 3

typedef void (*log_print_func)(const char *file, int line, int level, const char *message);

void LogSetLevel(int level);
void LogFormatPrint(const char *file, int line, int level, const char *format, ...);
void LogSetPrintFunction(log_print_func callback);

#define log_debug(FMT, ...) LogFormatPrint(__FILE__, __LINE__, kDebugLevel, FMT, ##__VA_ARGS__)
#define log_info(FMT, ...) LogFormatPrint(__FILE__, __LINE__, kInfoLevel, FMT, ##__VA_ARGS__)
#define log_warn(FMT, ...) LogFormatPrint(__FILE__, __LINE__, kWarnLevel, FMT, ##__VA_ARGS__)
#define log_error(FMT, ...) LogFormatPrint(__FILE__, __LINE__, kErrorLevel, FMT, ##__VA_ARGS__)

#define LOG_ASSERT(x)                                                                              \
    do {                                                                                           \
        if (RAPTOR_LIKELY(!(x))) {                                                                 \
            log_error("assertion failed: %s", #x);                                                 \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

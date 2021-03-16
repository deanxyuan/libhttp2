#include "src/utils/log.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#include <processthreadsapi.h>
#else
#include <pthread.h>
#endif

#include "src/utils/atomic.h"

namespace {
void DefalutLogPrint(const char *file, int line, int level, const char *message);
AtomicIntptr g_log_function((intptr_t)DefalutLogPrint);
AtomicInt32 g_min_level(0);
char g_level_char[4] = {'D', 'I', 'W', 'E'};
#ifdef _WIN32
constexpr char delimiter = '\\';
#define RAPTOR_LOG_FORMAT "[%s.%06d %5lu %c] %s (%s:%d)"
#else
constexpr char delimiter = '/';
#define RAPTOR_LOG_FORMAT "[%s.%06d %7lu %c] %s (%s:%d)"
#endif

int64_t GetCurrentMicroseconds() {
    auto n = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(n.time_since_epoch()).count();
}

void DefalutLogPrint(const char *file, int line, int level, const char *message) {
    const char *last_slash = NULL;
    const char *display_file = NULL;
    char time_buffer[64] = {0};

    last_slash = strrchr(file, delimiter);
    if (last_slash == NULL) {
        display_file = file;
    } else {
        display_file = last_slash + 1;
    }

    int64_t microsec = GetCurrentMicroseconds();
    time_t seconds = microsec / 1000000;
    int remain_microsec = static_cast<int>(microsec % 1000000);

#ifdef _WIN32
    auto threadid = static_cast<unsigned long>(GetCurrentThreadId());
    struct tm stm;
    if (localtime_s(&stm, &seconds)) {
        strcpy(time_buffer, "error:localtime");
    }
#else
    auto threadid = static_cast<unsigned long>(pthread_self());
    struct tm stm;
    if (!localtime_r(&seconds, &stm)) {
        strcpy(time_buffer, "error:localtime");
    }
#endif
    // "%F %T" 2020-05-10 01:43:06
    else if (0 == strftime(time_buffer, sizeof(time_buffer), "%F %T", &stm)) {
        strcpy(time_buffer, "error:strftime");
    }

    fprintf(stderr, RAPTOR_LOG_FORMAT, time_buffer,
            remain_microsec,  // microseconds
            threadid, g_level_char[level], message, display_file, line);

    fflush(stderr);
}
}  // namespace

void LogSetLevel(int level) {
    if (level < 0 || level > 3) {
        return;
    }
    g_min_level.Store(level);
}

void LogSetPrintFunction(log_print_func callback) {
    g_log_function.Store((intptr_t)callback);
}

void LogFormatPrint(const char *file, int line, int level, const char *format, ...) {

    char *message = NULL;
    va_list args;
    va_start(args, format);

#ifdef _WIN32
    int ret = _vscprintf(format, args);
    va_end(args);
    if (ret < 0) {
        return;
    }

    size_t buff_len = (size_t)ret + 1;
    message = (char *)malloc(buff_len);
    va_start(args, format);
    ret = vsnprintf_s(message, buff_len, _TRUNCATE, format, args);
    va_end(args);
#else
    if (vasprintf(&message, format, args) == -1) {  // stdio.h
        va_end(args);
        return;
    }
#endif
    if (g_min_level.Load() <= level) {
        ((log_print_func)g_log_function.Load())(file, line, level, message);
    }
    free(message);
}

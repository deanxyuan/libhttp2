/**
 * @file date.h
 * @brief HTTP/1.1 date string formatting utility.
 */

#pragma once
#include <time.h>
#include <string>

/**
 * @brief Builds an HTTP/1.1 formatted date string for the current time.
 * @return A string in RFC 7231 date format, e.g., "Tue, 03 Jul 2012 04:40:59 GMT".
 */
static inline std::string build_date_string() {
    char buf[64] = {0};
    time_t now = time(nullptr);
    if (now == static_cast<time_t>(-1)) {
        return std::string();  // time() failed
    }
    struct tm gmt_buf;
    struct tm *gmt = nullptr;
#ifdef _WIN32
    if (gmtime_s(&gmt_buf, &now) == 0) {
        gmt = &gmt_buf;
    }
#else
    gmt = gmtime_r(&now, &gmt_buf);
#endif
    if (!gmt) {
        return std::string();  // gmtime failed
    }
    strftime(buf, sizeof(buf), "%a, %d %h %Y %T GMT", gmt);  // strlen(buf) = 29
    return std::string(buf);
}

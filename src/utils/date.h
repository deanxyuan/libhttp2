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
    struct tm *gmt = gmtime(&now);
    strftime(buf, sizeof(buf), "%a, %d %h %G %T GMT", gmt);  // strlen(buf) = 29
    return std::string(buf);
}

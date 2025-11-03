#include "log.h"

// Implementation of JSON-line logging with UTC timestamps.
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

// Internal formatter.
static void vlogf(const char *level, const char *event, const char *fmt, va_list ap) {
    char ts[32];
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm);
    fprintf(stdout, "{\"ts\":\"%s\",\"level\":\"%s\",\"event\":\"%s\",\"msg\":\"",
            ts, level, event);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\"}\n");
    fflush(stdout);
}

void log_info(const char *event, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf("INFO", event, fmt, ap); va_end(ap);
}

void log_error(const char *event, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlogf("ERROR", event, fmt, ap); va_end(ap);
}



#include "debug_log.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static FILE* g_log_file = NULL;

static const char* timestamp(void) {
    static char buf[64];
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return buf;
}

void debug_log_init(void) {
    g_log_file = fopen("debug.log", "w");
    if (!g_log_file) return;
    setvbuf(g_log_file, NULL, _IONBF, 0); // unbuffered — survives crashes
    debug_log("=== Orbital Frontier Debug Log ===");

#ifdef _WIN32
    // Also attach a console for real-time output
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

void debug_log_shutdown(void) {
    if (g_log_file) {
        debug_log("=== Shutdown ===");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void debug_log(const char* fmt, ...) {
    if (!g_log_file) return;

    fprintf(g_log_file, "[%s] ", timestamp());

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);

    fprintf(g_log_file, "\n");

    // Also print to console
    printf("[%s] ", timestamp());
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void debug_sokol_logger(const char* tag, uint32_t log_level, uint32_t log_item,
                        const char* message, uint32_t line_nr, const char* filename,
                        void* user_data) {
    (void)user_data;
    const char* level_str = "???";
    switch (log_level) {
        case 0: level_str = "PANIC"; break;
        case 1: level_str = "ERROR"; break;
        case 2: level_str = "WARN"; break;
        case 3: level_str = "INFO"; break;
    }

    if (g_log_file) {
        fprintf(g_log_file, "[%s] [sokol][%s][%s][id:%u] %s:%u: %s\n",
                timestamp(), level_str, tag ? tag : "", log_item,
                filename ? filename : "?", line_nr,
                message ? message : "(no message)");
    }

    // Always print errors/panics to console
    if (log_level <= 1) {
        fprintf(stderr, "[sokol][%s][%s] %s (line %u in %s)\n",
                level_str, tag ? tag : "", message ? message : "", line_nr, filename ? filename : "?");
    }
}

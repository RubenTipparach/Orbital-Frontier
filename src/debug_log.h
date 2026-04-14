#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

// Call once at startup
void debug_log_init(void);

// Call at shutdown
void debug_log_shutdown(void);

// Log a message (writes to debug.log with timestamp)
void debug_log(const char* fmt, ...);

// Sokol-compatible log callback
void debug_sokol_logger(const char* tag, uint32_t log_level, uint32_t log_item,
                        const char* message, uint32_t line_nr, const char* filename,
                        void* user_data);

#endif

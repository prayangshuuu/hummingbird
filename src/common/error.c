/* error.c — thread-local error record (DD-011, DD-022).
 *
 * A status code says *what kind* of failure occurred; this record says *which
 * one, where, and why*. It is per-thread so concurrent operations never clobber
 * each other's diagnostics, and it stores the true OS error number (the platform
 * module turns that number into text — common never touches OS APIs).
 *
 * The record lives in thread-local storage and needs no initialization: C
 * zero-initializes it, which is exactly a clean HBI_OK state.
 */
#include "common/common_internal.h"

#include <stdio.h>
#include <string.h>

static HBI_THREAD_LOCAL hbi_error g_error; /* zero-init => clean HBI_OK state */

const hbi_error *hbi_error_last(void) {
    return &g_error;
}

void hbi_error_clear(void) {
    g_error.status = HBI_OK;
    g_error.os_errno = 0;
    g_error.file = NULL;
    g_error.line = 0;
    g_error.func = NULL;
    g_error.message[0] = '\0';
}

/* Shared tail: stamp the call-site fields and (already-formatted) message. */
static hbi_status error_commit(hbi_status status, int os_errno, const char *file, int line,
                               const char *func) {
    g_error.status = status;
    g_error.os_errno = os_errno;
    g_error.file = file;
    g_error.line = line;
    g_error.func = func;
    return status;
}

hbi_status hbi_error_set_at(hbi_status status, int os_errno, const char *file, int line,
                            const char *func, const char *msg) {
    if (status == HBI_OK) {
        hbi_error_clear();
        return HBI_OK;
    }
    if (msg != NULL) {
        /* strncpy would not guarantee termination; copy explicitly. */
        size_t n = strlen(msg);
        if (n >= sizeof(g_error.message)) {
            n = sizeof(g_error.message) - 1;
        }
        memcpy(g_error.message, msg, n);
        g_error.message[n] = '\0';
    } else {
        g_error.message[0] = '\0';
    }
    return error_commit(status, os_errno, file, line, func);
}

hbi_status hbi_error_setf_at(hbi_status status, int os_errno, const char *file, int line,
                             const char *func, const char *fmt, ...) {
    if (status == HBI_OK) {
        hbi_error_clear();
        return HBI_OK;
    }
    if (fmt != NULL) {
        va_list ap;
        va_start(ap, fmt);
        int written = vsnprintf(g_error.message, sizeof(g_error.message), fmt, ap);
        va_end(ap);
        if (written < 0) { /* encoding error: fall back to empty, not garbage */
            g_error.message[0] = '\0';
        }
    } else {
        g_error.message[0] = '\0';
    }
    return error_commit(status, os_errno, file, line, func);
}

int hbi_error_format(char *buf, size_t cap) {
    const hbi_error *e = &g_error;
    const char *file = e->file != NULL ? e->file : "?";
    const char *func = e->func != NULL ? e->func : "?";

    /* Two shapes: with and without a message body, to avoid a dangling ": ". */
    if (e->message[0] != '\0') {
        return snprintf(buf, cap, "%s: %s (os_errno=%d) at %s:%d in %s", hbi_status_str(e->status),
                        e->message, e->os_errno, file, e->line, func);
    }
    return snprintf(buf, cap, "%s (os_errno=%d) at %s:%d in %s", hbi_status_str(e->status),
                    e->os_errno, file, e->line, func);
}

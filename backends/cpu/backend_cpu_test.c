/* backend_cpu_test.c — smoke test for the reference CPU backend registration. */
#include "backend/backend.h"
#include "backend_cpu.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hb_backend_cpu_register() != HBI_OK) {
        fprintf(stderr, "cpu backend registration failed\n");
        return 1;
    }
    if (hbi_backend_count() != 1) {
        fprintf(stderr, "expected exactly one backend, got %d\n", hbi_backend_count());
        return 1;
    }
    const hbi_backend *b = hbi_backend_at(0);
    if (b == NULL || b->name == NULL || strcmp(b->name, "cpu") != 0) {
        fprintf(stderr, "registered backend is not the cpu backend\n");
        return 1;
    }
    if (hbi_backend_selftest() != HBI_OK) {
        fprintf(stderr, "backend registry selftest failed\n");
        return 1;
    }
    printf("[ok] cpu backend registered\n");
    return 0;
}

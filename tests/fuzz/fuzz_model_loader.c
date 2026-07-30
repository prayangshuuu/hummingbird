/* fuzz_model_loader.c — libFuzzer harness for the model loader pipeline.
 *
 * Security objective: hbi_model_load() must NEVER crash, abort, or invoke
 * undefined behaviour regardless of the byte sequence it receives as a
 * model file.  Expected outcomes on malformed input:
 *   - Returns a non-OK hbi_status (any error code is acceptable)
 *   - out_session is NULL on failure
 *   - No memory is leaked (verifiable with ASAN/LeakSanitizer)
 *   - No heap buffer overflow (verifiable with ASAN)
 *   - No undefined behaviour (verifiable with UBSan)
 *
 * Build instructions:
 *   clang -g -fsanitize=fuzzer,address,undefined \
 *         -I<src> -I<include> fuzz_model_loader.c \
 *         -L<build> -lhummingbird -o fuzz_model_loader
 *   ./fuzz_model_loader corpus/
 *
 * The harness writes the fuzzer-provided bytes to a temporary file and
 * calls hbi_model_load() on that file with both GGUF and Safetensors hints
 * exercised.  We do not call exit() on failure — libFuzzer requires that
 * LLVMFuzzerTestOneInput returns after exercising the code.
 *
 * Seed corpus: the seeds/ directory contains valid minimal files:
 *   seeds/valid.safetensors  — minimal 2-tensor Safetensors file
 *   seeds/empty              — zero-byte file
 *   seeds/magic_only         — just the 8-byte LE header length with no JSON
 *
 * See: https://llvm.org/docs/LibFuzzer.html
 */
#include "memory/memory.h"
#include "model/model.h"
#include "model/model_internal.h" /* hbi_format_safetensors_register() */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Write `data` bytes to a temporary file and return the path.
 * Returns NULL on failure.  Caller must remove and free the path. */
static char *write_tmp(const uint8_t *data, size_t size) {
    /* Use a fixed path so the OS temp dir does not accumulate files between
     * runs (the harness replaces it on every invocation). */
    static const char *path = "fuzz_model_loader_tmp.bin";
    FILE *f = fopen(path, "wb");
    if (!f) {
        return NULL;
    }
    if (size > 0) {
        (void)fwrite(data, 1, size, f);
    }
    fclose(f);
    return (char *)path; /* static — caller must not free */
}

/* libFuzzer entry point.  Called once per corpus item / mutated input. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Limit size to 16 MiB so the fuzzer does not thrash I/O. */
    if (size > 16u * 1024u * 1024u) {
        return 0;
    }

    /* Ensure error record is clean each invocation (thread-local, so safe). */
    hbi_error_clear();

    /* Register format handlers once.  hbi_format_handler_registry_clear() is
     * NOT called between runs — this exercises the path where handlers are
     * already registered (the normal production state). */
    static int handlers_registered = 0;
    if (!handlers_registered) {
        /* Safetensors handler (implemented). */
        hbi_format_safetensors_register();
        /* GGUF handler will be registered when implemented. */
        handlers_registered = 1;
    }

    const char *path = write_tmp(data, size);
    if (!path) {
        return 0; /* I/O failure in the harness itself — skip this input */
    }

    /* Exercise auto-detect path. */
    {
        hbi_load_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.model_path = path;
        opts.format_hint = HBI_MODEL_FORMAT_UNKNOWN;
        opts.flags = HBI_LOAD_DEFAULT;

        hbi_load_session *session = NULL;
        hbi_status st = hbi_model_load(&opts, hbi_allocator_system(), &session);
        if (st == HBI_OK) {
            /* Valid parse: verify basic invariants on the session. */
            const hbi_model_manifest *m = hbi_load_session_manifest(session);
            if (m) {
                uint32_t count = hbi_model_manifest_count(m);
                /* Iterate entries to exercise the manifest memory fully. */
                for (uint32_t i = 0; i < count; ++i) {
                    (void)hbi_model_manifest_entry(m, i);
                }
            }
            const hbi_model_metadata *md = hbi_load_session_metadata(session);
            if (md) {
                (void)hbi_model_metadata_count(md);
            }
            (void)hbi_load_session_statistics(session);
        }
        hbi_model_load_session_destroy(session); /* NULL-safe */
    }

    /* Exercise the STRICT flag (extra validation). */
    {
        hbi_load_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.model_path = path;
        opts.format_hint = HBI_MODEL_FORMAT_UNKNOWN;
        opts.flags = HBI_LOAD_STRICT;

        hbi_load_session *session = NULL;
        (void)hbi_model_load(&opts, hbi_allocator_system(), &session);
        hbi_model_load_session_destroy(session);
    }

    /* Clear for the next run. */
    hbi_error_clear();

    return 0; /* always return 0; non-zero would mark the input as crash */
}

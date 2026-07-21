/* main.c — the `hb` command-line frontend.
 *
 * PHASE 4 scaffold: only `--version`/`--help` and a subcommand dispatch stub.
 * Subcommands (run/chat/serve/convert/bench/doctor/plan) are declared in the
 * usage text but not yet implemented — they return a clear "not implemented"
 * message and a nonzero exit code rather than pretending to work.
 *
 * A frontend is allowed to call exit()/return from main (ADR DD-011); the
 * library it links against is not.
 */
#include <hummingbird/hummingbird.h>

#include <stdio.h>
#include <string.h>

static int usage(FILE *out) {
    fprintf(out,
            "hb — Hummingbird inference runtime (v%s)\n"
            "\n"
            "usage: hb <command> [options]\n"
            "\n"
            "commands (planned; not yet implemented):\n"
            "  run       one-shot prompt completion\n"
            "  chat      interactive session\n"
            "  serve     OpenAI-compatible HTTP server\n"
            "  convert   produce a .hbm model container\n"
            "  bench     run benchmarks\n"
            "  doctor    inspect environment and model fit\n"
            "  plan      show the placement/memory plan for a model\n"
            "\n"
            "options:\n"
            "  -h, --help       show this help and exit\n"
            "  -V, --version    print version and exit\n",
            hb_version_string());
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        return usage(stdout);
    }
    if (strcmp(cmd, "-V") == 0 || strcmp(cmd, "--version") == 0) {
        printf("hb %s (ABI %d)\n", hb_version_string(), hb_version());
        return 0;
    }

    /* Known-but-unimplemented subcommands fail honestly (ADR DD-011). */
    static const char *const known[] = {"run",   "chat",   "serve", "convert",
                                        "bench", "doctor", "plan"};
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strcmp(cmd, known[i]) == 0) {
            fprintf(stderr,
                    "hb: '%s' is not implemented yet (scaffold). "
                    "See docs/architecture and the roadmap in .claude/PROJECT_CONTEXT.md.\n",
                    cmd);
            return (int)HB_ERR_NOT_IMPLEMENTED;
        }
    }

    fprintf(stderr, "hb: unknown command '%s'\n\n", cmd);
    usage(stderr);
    return 2;
}

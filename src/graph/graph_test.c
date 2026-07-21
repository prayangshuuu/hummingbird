/* graph_test.c — unit-test placeholder for the `graph` module.
 * Replace with real cases as the module gains behavior. */
#include "graph/graph.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (hbi_graph_selftest() != HBI_OK) {
        fprintf(stderr, "%s: selftest failed\n", hbi_graph_name());
        return 1;
    }
    if (strcmp(hbi_graph_name(), "graph") != 0) {
        fprintf(stderr, "unexpected module name: %s\n", hbi_graph_name());
        return 1;
    }
    printf("[ok] %s\n", hbi_graph_name());
    return 0;
}

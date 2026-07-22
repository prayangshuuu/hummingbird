/* device_test.c — tests for the Device Manager (RFC-006) */
#include "device/device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HBI_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define HBI_CHECK_EQ_INT(a, b) HBI_CHECK((a) == (b))
#define HBI_CHECK_STR_EQ(a, b) HBI_CHECK(strcmp((a), (b)) == 0)

static void test_device_manager(void) {
    hbi_device_manager *mgr = NULL;

    HBI_CHECK_EQ_INT(hbi_device_manager_create(NULL), HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_device_manager_create(&mgr), HBI_OK);
    HBI_CHECK(mgr != NULL);

    uint32_t count = hbi_device_manager_get_device_count(mgr);
    HBI_CHECK(count >= 1); /* Should always discover at least the CPU */

    const hbi_device *dev = hbi_device_manager_get_device(mgr, 0);
    HBI_CHECK(dev != NULL);

    const hbi_device *best = hbi_device_manager_get_best(mgr);
    HBI_CHECK(best != NULL);

    HBI_CHECK_EQ_INT(hbi_device_get_type(dev), HBI_DEVICE_TYPE_CPU);

    hbi_device_capabilities caps = hbi_device_get_capabilities(dev);
    /* Should at least be UMA */
    HBI_CHECK((caps & HBI_CAP_UMA) != 0);

    hbi_device_info info;
    HBI_CHECK_EQ_INT(hbi_device_get_info(dev, &info), HBI_OK);
    HBI_CHECK(info.logical_cores >= 1);
    HBI_CHECK(info.physical_cores >= 1);
    HBI_CHECK(info.cacheline_size > 0);
    HBI_CHECK(strlen(info.arch) > 0);
    HBI_CHECK(strlen(info.vendor) > 0);
    HBI_CHECK(strlen(info.name) > 0);

    hbi_device_memory mem;
    HBI_CHECK_EQ_INT(hbi_device_get_memory(dev, &mem), HBI_OK);
    HBI_CHECK(mem.total_bytes > 0);
    HBI_CHECK(mem.num_regions >= 1);
    HBI_CHECK(mem.regions[0].total_bytes > 0);

    hbi_device_statistics stats;
    HBI_CHECK_EQ_INT(hbi_device_get_statistics(dev, &stats), HBI_OK);
    HBI_CHECK_EQ_INT((int)stats.currently_allocated_bytes, 0);

    char buf[128];
    int n = hbi_device_describe(dev, buf, sizeof(buf));
    HBI_CHECK(n > 0);

    hbi_device_manager_destroy(mgr);
}

int main(void) {
    HBI_CHECK_EQ_INT(hbi_device_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_device_module_name(), "device");

    test_device_manager();

    printf("PASS\n");
    return 0;
}

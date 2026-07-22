/* device_internal.h — Internal state for Device Manager */
#ifndef HB_DEVICE_INTERNAL_H
#define HB_DEVICE_INTERNAL_H

#include "device.h"

#define HBI_MAX_DEVICES 8

struct hbi_device {
    uint32_t id;
    hbi_device_type type;
    hbi_device_capabilities capabilities;

    hbi_device_info info;
    hbi_device_memory memory;
    hbi_device_statistics stats;
};

struct hbi_device_manager {
    uint32_t num_devices;
    hbi_device devices[HBI_MAX_DEVICES];
};

/* Internal initialization functions */
hbi_status hbi_device_cpu_discover(hbi_device *device);

#endif /* HB_DEVICE_INTERNAL_H */

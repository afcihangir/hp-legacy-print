#include "hplp/usb_power.h"
#include "hplp/models.h"

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file;

    if (buffer_size == 0) {
        return -1;
    }

    buffer[0] = '\0';
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    if (fgets(buffer, (int)buffer_size, file) == NULL) {
        fclose(file);
        return -1;
    }

    fclose(file);

    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 0;
}

static int build_sysfs_name(libusb_device *device, char *buffer, size_t buffer_size)
{
    uint8_t ports[8];
    int port_count;
    int written;
    size_t used;

    if (buffer_size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    port_count = libusb_get_port_numbers(device, ports, sizeof(ports));
    if (port_count <= 0) {
        return -1;
    }

    written = snprintf(buffer, buffer_size, "%u-%u",
                       libusb_get_bus_number(device), ports[0]);
    if (written < 0 || (size_t)written >= buffer_size) {
        return -1;
    }

    used = (size_t)written;

    for (int i = 1; i < port_count; ++i) {
        written = snprintf(buffer + used, buffer_size - used,
                           ".%u", ports[i]);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            return -1;
        }
        used += (size_t)written;
    }

    return 0;
}

static void print_value(const char *sysfs_name,
                        const char *relative_path,
                        const char *label)
{
    char path[512];
    char value[256];

    snprintf(path, sizeof(path),
             "/sys/bus/usb/devices/%s/%s",
             sysfs_name, relative_path);

    if (read_text_file(path, value, sizeof(value)) == 0) {
        printf("  %s=%s\n", label, value);
    } else {
        printf("  %s=unavailable\n", label);
    }
}

int hplp_usb_power_info_supported(void)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    ssize_t total;
    int found = 0;

    int rc = libusb_init(&context);
    if (rc != 0) {
        fprintf(stderr, "USB init failed: %d\n", rc);
        return 1;
    }

    total = libusb_get_device_list(context, &list);
    if (total < 0) {
        fprintf(stderr, "USB device list failed: %zd\n", total);
        libusb_exit(context);
        return 1;
    }

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        const hplp_model_t *model =
            hplp_model_lookup(descriptor.idVendor, descriptor.idProduct);

        if (model == NULL) {
            continue;
        }

        found = 1;

        char sysfs_name[64];
        if (build_sysfs_name(list[i], sysfs_name, sizeof(sysfs_name)) != 0) {
            printf("DEVICE %s %04x:%04x\n",
                   model->model,
                   descriptor.idVendor,
                   descriptor.idProduct);
            puts("  sysfs=unavailable");
            continue;
        }

        printf("DEVICE %s %04x:%04x sysfs=%s\n",
               model->model,
               descriptor.idVendor,
               descriptor.idProduct,
               sysfs_name);

        print_value(sysfs_name, "power/control", "power.control");
        print_value(sysfs_name, "power/runtime_status", "power.runtime_status");
        print_value(sysfs_name, "power/autosuspend", "power.autosuspend");
        print_value(sysfs_name, "power/autosuspend_delay_ms",
                    "power.autosuspend_delay_ms");
        print_value(sysfs_name, "power/wakeup", "power.wakeup");
    }

    libusb_free_device_list(list, 1);
    libusb_exit(context);

    if (!found) {
        puts("No supported HP LaserJet printer found.");
    }

    return 0;
}

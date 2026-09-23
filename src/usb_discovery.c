#include "hplp/usb_discovery.h"

#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>

static hplp_serial_state_t read_serial(
    libusb_device *device,
    const struct libusb_device_descriptor *descriptor,
    char *buffer,
    size_t buffer_size,
    int *error_code)
{
    libusb_device_handle *handle = NULL;

    if (error_code != NULL) {
        *error_code = 0;
    }

    if (buffer_size == 0) {
        if (error_code != NULL) {
            *error_code = LIBUSB_ERROR_INVALID_PARAM;
        }
        return HPLP_SERIAL_PENDING;
    }

    buffer[0] = '\0';

    if (descriptor->iSerialNumber == 0) {
        return HPLP_SERIAL_ABSENT;
    }

    int rc = libusb_open(device, &handle);
    if (rc != 0) {
        if (error_code != NULL) {
            *error_code = rc;
        }
        return HPLP_SERIAL_PENDING;
    }

    int length = libusb_get_string_descriptor_ascii(
        handle,
        descriptor->iSerialNumber,
        (unsigned char *)buffer,
        (int)(buffer_size - 1)
    );

    if (length > 0) {
        buffer[length] = '\0';
        libusb_close(handle);
        return HPLP_SERIAL_READY;
    }

    buffer[0] = '\0';
    if (error_code != NULL) {
        *error_code = length < 0 ? length : LIBUSB_ERROR_IO;
    }

    libusb_close(handle);
    return HPLP_SERIAL_PENDING;
}

int hplp_usb_list(hplp_usb_device_t **devices, size_t *count)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    ssize_t total;
    size_t matched = 0;
    hplp_usb_device_t *result = NULL;

    if (devices == NULL || count == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    *devices = NULL;
    *count = 0;

    int rc = libusb_init(&context);
    if (rc != 0) {
        return rc;
    }

    total = libusb_get_device_list(context, &list);
    if (total < 0) {
        libusb_exit(context);
        return (int)total;
    }

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        if (hplp_model_lookup(descriptor.idVendor, descriptor.idProduct) != NULL) {
            ++matched;
        }
    }

    if (matched > 0) {
        result = calloc(matched, sizeof(*result));
        if (result == NULL) {
            libusb_free_device_list(list, 1);
            libusb_exit(context);
            return LIBUSB_ERROR_NO_MEM;
        }
    }

    size_t out = 0;
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

        result[out].bus_number = libusb_get_bus_number(list[i]);
        result[out].device_address = libusb_get_device_address(list[i]);
        result[out].vendor_id = descriptor.idVendor;
        result[out].product_id = descriptor.idProduct;
        result[out].model = model;

        result[out].serial_state = read_serial(
            list[i],
            &descriptor,
            result[out].serial,
            sizeof(result[out].serial),
            &result[out].serial_error
        );

        ++out;
    }

    libusb_free_device_list(list, 1);
    libusb_exit(context);

    *devices = result;
    *count = out;
    return 0;
}

void hplp_usb_list_free(hplp_usb_device_t *devices)
{
    free(devices);
}

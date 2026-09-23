#define _POSIX_C_SOURCE 200809L

#include "hplp/usb_transport.h"
#include "hplp/models.h"

#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HPLP_USB_TIMEOUT_MS 10000
#define HPLP_USB_CHUNK_SIZE 16384

static volatile sig_atomic_t transport_running = 1;

struct hplp_usb_session {
    libusb_context *context;
    libusb_device_handle *handle;
    int interface_number;
    int detached_kernel_driver;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t bus_number;
    uint8_t device_address;
    char serial[256];
    const hplp_model_t *model;
    size_t bytes_accepted;
    int last_error;
};

static void transport_signal(int signal_number)
{
    (void)signal_number;
    transport_running = 0;
}

static void sleep_one_second(void)
{
    struct timespec duration = {1, 0};
    nanosleep(&duration, NULL);
}

static int read_serial_handle(libusb_device_handle *handle,
                              const struct libusb_device_descriptor *descriptor,
                              char *buffer,
                              size_t buffer_size)
{
    int length;

    if (buffer_size == 0) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    buffer[0] = '\0';

    if (descriptor->iSerialNumber == 0) {
        return 0;
    }

    length = libusb_get_string_descriptor_ascii(
        handle,
        descriptor->iSerialNumber,
        (unsigned char *)buffer,
        (int)(buffer_size - 1)
    );

    if (length > 0) {
        buffer[length] = '\0';
        return 0;
    }

    buffer[0] = '\0';
    return length < 0 ? length : LIBUSB_ERROR_IO;
}

static void read_serial(libusb_device *device,
                        const struct libusb_device_descriptor *descriptor,
                        char *buffer,
                        size_t buffer_size)
{
    libusb_device_handle *handle = NULL;

    if (buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';

    if (descriptor->iSerialNumber == 0) {
        return;
    }

    if (libusb_open(device, &handle) != 0) {
        return;
    }

    (void)read_serial_handle(handle, descriptor, buffer, buffer_size);
    libusb_close(handle);
}

static int same_physical_device(libusb_device *device,
                                const hplp_usb_session_t *session)
{
    struct libusb_device_descriptor descriptor;

    if (libusb_get_device_descriptor(device, &descriptor) != 0) {
        return 0;
    }

    if (descriptor.idVendor != session->vendor_id ||
        descriptor.idProduct != session->product_id) {
        return 0;
    }

    if (session->serial[0] != '\0') {
        char serial[256];
        read_serial(device, &descriptor, serial, sizeof(serial));

        if (serial[0] != '\0') {
            return strcmp(serial, session->serial) == 0;
        }
    }

    return libusb_get_bus_number(device) == session->bus_number &&
           libusb_get_device_address(device) == session->device_address;
}

static int session_still_present(libusb_context *context,
                                 const hplp_usb_session_t *session)
{
    libusb_device **list = NULL;
    ssize_t total = libusb_get_device_list(context, &list);
    int present = 0;

    if (total < 0) {
        return 0;
    }

    for (ssize_t i = 0; i < total; ++i) {
        if (same_physical_device(list[i], session)) {
            present = 1;
            break;
        }
    }

    libusb_free_device_list(list, 1);
    return present;
}

static void close_session_handle(hplp_usb_session_t *session,
                                 int restore_kernel_driver)
{
    if (session->handle == NULL) {
        return;
    }

    if (session->interface_number >= 0) {
        (void)libusb_release_interface(session->handle,
                                       session->interface_number);

        if (restore_kernel_driver &&
            session->detached_kernel_driver) {
            (void)libusb_attach_kernel_driver(session->handle,
                                              session->interface_number);
        }
    }

    libusb_close(session->handle);
    session->handle = NULL;
    session->interface_number = -1;
    session->detached_kernel_driver = 0;
}

static int claim_device(libusb_device *device,
                        const struct libusb_device_descriptor *descriptor,
                        const hplp_model_t *model,
                        const char *expected_serial,
                        hplp_usb_session_t *session)
{
    libusb_device_handle *handle = NULL;
    char serial[256] = "";
    int detached = 0;
    int rc;

    rc = libusb_open(device, &handle);
    if (rc != 0) {
        return rc;
    }

    rc = read_serial_handle(handle, descriptor, serial, sizeof(serial));
    if (expected_serial && expected_serial[0]) {
        if (rc != 0) {
            libusb_close(handle);
            return rc;
        }

        if (serial[0] == '\0' || strcmp(serial, expected_serial) != 0) {
            libusb_close(handle);
            return LIBUSB_ERROR_NO_DEVICE;
        }
    }

    rc = libusb_kernel_driver_active(handle, model->printer_interface);

    if (rc == 1) {
        rc = libusb_detach_kernel_driver(handle, model->printer_interface);

        if (rc != 0) {
            libusb_close(handle);
            return rc;
        }

        detached = 1;
    } else if (rc != 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED) {
        libusb_close(handle);
        return rc;
    }

    rc = libusb_claim_interface(handle, model->printer_interface);
    if (rc != 0) {
        if (detached) {
            (void)libusb_attach_kernel_driver(handle,
                                              model->printer_interface);
        }
        libusb_close(handle);
        return rc;
    }

    session->handle = handle;
    session->interface_number = model->printer_interface;
    session->detached_kernel_driver = detached;
    session->vendor_id = descriptor->idVendor;
    session->product_id = descriptor->idProduct;
    session->bus_number = libusb_get_bus_number(device);
    session->device_address = libusb_get_device_address(device);
    session->model = model;
    session->bytes_accepted = 0;
    session->last_error = 0;

    if (serial[0] != '\0') {
        snprintf(session->serial, sizeof(session->serial), "%s", serial);
    } else {
        session->serial[0] = '\0';
    }

    return 0;
}

static int open_first_supported(libusb_context *context,
                                hplp_usb_session_t *session)
{
    libusb_device **list = NULL;
    ssize_t total = libusb_get_device_list(context, &list);
    int result = 0;

    if (total < 0) {
        return (int)total;
    }

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;
        const hplp_model_t *model;
        int rc;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        model = hplp_model_lookup(descriptor.idVendor,
                                  descriptor.idProduct);

        if (model == NULL ||
            model->printer_interface < 0 ||
            model->bulk_out_endpoint < 0) {
            continue;
        }

        rc = claim_device(list[i], &descriptor, model, NULL, session);
        if (rc == 0) {
            result = 1;
            break;
        }

        result = rc;
    }

    libusb_free_device_list(list, 1);
    return result;
}

int hplp_usb_session_open(uint16_t vendor_id,
                          uint16_t product_id,
                          const char *serial,
                          hplp_usb_session_t **session_out)
{
    hplp_usb_session_t *session;
    const hplp_model_t *model;
    libusb_device **list = NULL;
    ssize_t total;
    int result = LIBUSB_ERROR_NO_DEVICE;
    int rc;

    if (!session_out || !serial || !serial[0]) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    *session_out = NULL;

    model = hplp_model_lookup(vendor_id, product_id);
    if (!model ||
        model->printer_interface < 0 ||
        model->bulk_out_endpoint < 0) {
        return LIBUSB_ERROR_NOT_SUPPORTED;
    }

    session = calloc(1, sizeof(*session));
    if (!session) {
        return LIBUSB_ERROR_NO_MEM;
    }

    session->interface_number = -1;

    rc = libusb_init(&session->context);
    if (rc != 0) {
        free(session);
        return rc;
    }

    total = libusb_get_device_list(session->context, &list);
    if (total < 0) {
        result = (int)total;
        goto fail;
    }

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        if (descriptor.idVendor != vendor_id ||
            descriptor.idProduct != product_id) {
            continue;
        }

        rc = claim_device(list[i], &descriptor, model, serial, session);
        if (rc == 0) {
            result = 0;
            break;
        }

        if (rc != LIBUSB_ERROR_NO_DEVICE) {
            result = rc;
        }
    }

    libusb_free_device_list(list, 1);
    list = NULL;

    if (result == 0) {
        *session_out = session;
        return 0;
    }

fail:
    if (list) {
        libusb_free_device_list(list, 1);
    }
    libusb_exit(session->context);
    free(session);
    return result;
}

ssize_t hplp_usb_session_write(hplp_usb_session_t *session,
                               const void *buffer,
                               size_t bytes)
{
    const unsigned char *data = (const unsigned char *)buffer;
    size_t offset = 0;

    if (!session || !session->handle || (!buffer && bytes > 0)) {
        if (session) {
            session->last_error = LIBUSB_ERROR_INVALID_PARAM;
        }
        return -1;
    }

    session->last_error = 0;

    while (offset < bytes) {
        size_t remaining = bytes - offset;
        int chunk = remaining > HPLP_USB_CHUNK_SIZE
                        ? HPLP_USB_CHUNK_SIZE
                        : (int)remaining;
        int transferred = 0;
        int rc = libusb_bulk_transfer(
            session->handle,
            (unsigned char)session->model->bulk_out_endpoint,
            (unsigned char *)(data + offset),
            chunk,
            &transferred,
            HPLP_USB_TIMEOUT_MS
        );

        if (transferred > 0) {
            offset += (size_t)transferred;
            session->bytes_accepted += (size_t)transferred;
        }

        if (rc != 0) {
            session->last_error = rc;
            return -1;
        }

        if (transferred == 0) {
            session->last_error = LIBUSB_ERROR_IO;
            return -1;
        }
    }

    return (ssize_t)offset;
}

void hplp_usb_session_close(hplp_usb_session_t *session)
{
    libusb_context *context;
    int restore_kernel_driver;

    if (!session) {
        return;
    }

    context = session->context;
    restore_kernel_driver =
        session->model && !session->model->no_reattach;

    close_session_handle(session, restore_kernel_driver);

    if (context) {
        libusb_exit(context);
    }

    free(session);
}

size_t hplp_usb_session_bytes_accepted(const hplp_usb_session_t *session)
{
    return session ? session->bytes_accepted : 0;
}

int hplp_usb_session_last_error(const hplp_usb_session_t *session)
{
    return session ? session->last_error : LIBUSB_ERROR_INVALID_PARAM;
}

const char *hplp_usb_error_string(int error_code)
{
    if (error_code == 0) {
        return "success";
    }

    return libusb_error_name(error_code);
}

static void print_ready(const hplp_usb_session_t *session)
{
    printf("STATE READY %s %04x:%04x bus=%u address=%u",
           session->model->model,
           session->vendor_id,
           session->product_id,
           session->bus_number,
           session->device_address);

    if (session->serial[0] != '\0') {
        printf(" serial=%s", session->serial);
    }

    printf(" interface=%d out=0x%02x in=0x%02x\n",
           session->model->printer_interface,
           session->model->bulk_out_endpoint,
           session->model->bulk_in_endpoint);

    fflush(stdout);
}

int hplp_usb_transport_watch(void)
{
    libusb_context *context = NULL;
    hplp_usb_session_t session;
    int last_state = -1;
    int rc;

    memset(&session, 0, sizeof(session));
    session.interface_number = -1;

    rc = libusb_init(&context);
    if (rc != 0) {
        fprintf(stderr, "USB init failed: %d\n", rc);
        return 1;
    }

    transport_running = 1;
    signal(SIGINT, transport_signal);
    signal(SIGTERM, transport_signal);

    puts("Transport watch started.");
    puts("No print data will be sent. Existing CUPS USB access is unavailable while this test is running.");

    while (transport_running) {
        if (session.handle == NULL) {
            rc = open_first_supported(context, &session);

            if (rc == 1) {
                print_ready(&session);
                last_state = 1;
            } else if (last_state != 0) {
                puts("STATE DISCONNECTED");
                fflush(stdout);
                last_state = 0;
            }
        } else if (!session_still_present(context, &session)) {
            puts("STATE DISCONNECTED");
            fflush(stdout);

            /*
             * The physical device is gone, so reattaching usblp is
             * meaningless. Close the stale handle and wait for a
             * newly enumerated device.
             */
            close_session_handle(&session, 0);
            last_state = 0;
        }

        sleep_one_second();
    }

    /*
     * This command is a diagnostic. On a normal Ctrl+C exit restore
     * the original usblp state so the user's current CUPS setup keeps
     * working. The production PAPPL path instead honors the model's
     * no-reattach policy when its hplp:// session closes.
     */
    close_session_handle(&session, 1);
    libusb_exit(context);

    puts("Transport watch stopped; original kernel-driver state restored.");
    return 0;
}

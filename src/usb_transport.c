#define _POSIX_C_SOURCE 200809L

#include "hplp/usb_transport.h"
#include "hplp/models.h"

#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t transport_running = 1;

typedef struct {
    libusb_device_handle *handle;
    int interface_number;
    int detached_kernel_driver;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t bus_number;
    uint8_t device_address;
    char serial[256];
    const hplp_model_t *model;
} hplp_transport_session_t;

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

    int length = libusb_get_string_descriptor_ascii(
        handle,
        descriptor->iSerialNumber,
        (unsigned char *)buffer,
        (int)(buffer_size - 1)
    );

    if (length > 0) {
        buffer[length] = '\0';
    }

    libusb_close(handle);
}

static int same_physical_device(libusb_device *device,
                                const hplp_transport_session_t *session)
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
                                 const hplp_transport_session_t *session)
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

static void close_session(hplp_transport_session_t *session,
                          int restore_kernel_driver)
{
    if (session->handle == NULL) {
        return;
    }

    if (session->interface_number >= 0) {
        libusb_release_interface(session->handle,
                                 session->interface_number);

        if (restore_kernel_driver &&
            session->detached_kernel_driver) {
            libusb_attach_kernel_driver(session->handle,
                                        session->interface_number);
        }
    }

    libusb_close(session->handle);
    memset(session, 0, sizeof(*session));
    session->interface_number = -1;
}

static int open_first_supported(libusb_context *context,
                                hplp_transport_session_t *session)
{
    libusb_device **list = NULL;
    ssize_t total = libusb_get_device_list(context, &list);

    if (total < 0) {
        return (int)total;
    }

    int result = 0;

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        const hplp_model_t *model =
            hplp_model_lookup(descriptor.idVendor,
                              descriptor.idProduct);

        if (model == NULL || model->printer_interface < 0) {
            continue;
        }

        libusb_device_handle *handle = NULL;
        int rc = libusb_open(list[i], &handle);

        if (rc != 0) {
            result = rc;
            continue;
        }

        int detached = 0;
        rc = libusb_kernel_driver_active(
            handle, model->printer_interface);

        if (rc == 1) {
            rc = libusb_detach_kernel_driver(
                handle, model->printer_interface);

            if (rc != 0) {
                libusb_close(handle);
                result = rc;
                continue;
            }

            detached = 1;
        } else if (rc != 0 &&
                   rc != LIBUSB_ERROR_NOT_SUPPORTED) {
            libusb_close(handle);
            result = rc;
            continue;
        }

        rc = libusb_claim_interface(
            handle, model->printer_interface);

        if (rc != 0) {
            if (detached) {
                libusb_attach_kernel_driver(
                    handle, model->printer_interface);
            }
            libusb_close(handle);
            result = rc;
            continue;
        }

        memset(session, 0, sizeof(*session));
        session->handle = handle;
        session->interface_number =
            model->printer_interface;
        session->detached_kernel_driver = detached;
        session->vendor_id = descriptor.idVendor;
        session->product_id = descriptor.idProduct;
        session->bus_number =
            libusb_get_bus_number(list[i]);
        session->device_address =
            libusb_get_device_address(list[i]);
        session->model = model;

        read_serial(list[i], &descriptor,
                    session->serial,
                    sizeof(session->serial));

        result = 1;
        break;
    }

    libusb_free_device_list(list, 1);
    return result;
}

static void print_ready(const hplp_transport_session_t *session)
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
    hplp_transport_session_t session;
    int last_state = -1;

    memset(&session, 0, sizeof(session));
    session.interface_number = -1;

    int rc = libusb_init(&context);
    if (rc != 0) {
        fprintf(stderr, "USB init failed: %d\n", rc);
        return 1;
    }

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
            } else {
                if (last_state != 0) {
                    puts("STATE DISCONNECTED");
                    fflush(stdout);
                    last_state = 0;
                }
            }
        } else if (!session_still_present(context, &session)) {
            puts("STATE DISCONNECTED");
            fflush(stdout);

            /*
             * The physical device is gone, so reattaching usblp is
             * meaningless. Close the stale handle and wait for a
             * newly enumerated device.
             */
            close_session(&session, 0);
            last_state = 0;
        }

        sleep_one_second();
    }

    /*
     * This command is a diagnostic. On a normal Ctrl+C exit restore
     * the original usblp state so the user's current CUPS setup keeps
     * working. The production daemon will instead own the interface
     * continuously and honor the model's no-reattach policy.
     */
    close_session(&session, 1);
    libusb_exit(context);

    puts("Transport watch stopped; original kernel-driver state restored.");
    return 0;
}

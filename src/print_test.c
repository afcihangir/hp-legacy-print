#define _POSIX_C_SOURCE 200809L

#include "hplp/print_test.h"
#include "hplp/render_test.h"

#include <errno.h>
#include <fcntl.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define P1102_VENDOR_ID 0x03f0
#define P1102_PRODUCT_ID 0x002a
#define P1102_INTERFACE 0
#define P1102_BULK_OUT 0x01
#define USB_TIMEOUT_MS 10000
#define USB_CHUNK_SIZE 16384

static libusb_device_handle *open_p1102(libusb_context *context,
                                        int *detached_out)
{
    libusb_device **list = NULL;
    libusb_device_handle *handle = NULL;
    ssize_t total;

    *detached_out = 0;

    total = libusb_get_device_list(context, &list);
    if (total < 0) {
        return NULL;
    }

    for (ssize_t i = 0; i < total; ++i) {
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(list[i], &descriptor) != 0) {
            continue;
        }

        if (descriptor.idVendor != P1102_VENDOR_ID ||
            descriptor.idProduct != P1102_PRODUCT_ID) {
            continue;
        }

        if (libusb_open(list[i], &handle) != 0) {
            handle = NULL;
            continue;
        }

        int active = libusb_kernel_driver_active(handle, P1102_INTERFACE);
        if (active == 1) {
            int rc = libusb_detach_kernel_driver(handle, P1102_INTERFACE);
            if (rc != 0) {
                libusb_close(handle);
                handle = NULL;
                continue;
            }
            *detached_out = 1;
        } else if (active != 0 && active != LIBUSB_ERROR_NOT_SUPPORTED) {
            libusb_close(handle);
            handle = NULL;
            continue;
        }

        if (libusb_claim_interface(handle, P1102_INTERFACE) != 0) {
            if (*detached_out) {
                libusb_attach_kernel_driver(handle, P1102_INTERFACE);
            }
            libusb_close(handle);
            handle = NULL;
            *detached_out = 0;
            continue;
        }

        break;
    }

    libusb_free_device_list(list, 1);
    return handle;
}

static int send_file(libusb_device_handle *handle,
                     const char *path,
                     long long expected_bytes)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open ZJS");
        return 1;
    }

    unsigned char buffer[USB_CHUNK_SIZE];
    long long total_sent = 0;

    for (;;) {
        ssize_t got = read(fd, buffer, sizeof(buffer));

        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("read ZJS");
            close(fd);
            return 1;
        }

        if (got == 0) {
            break;
        }

        int offset = 0;

        while (offset < got) {
            int transferred = 0;
            int rc = libusb_bulk_transfer(
                handle,
                P1102_BULK_OUT,
                buffer + offset,
                (int)(got - offset),
                &transferred,
                USB_TIMEOUT_MS
            );

            if (transferred > 0) {
                offset += transferred;
                total_sent += transferred;
            }

            if (rc != 0) {
                fprintf(stderr,
                        "USB send failed: error=%d after=%lld bytes",
                        rc,
                        total_sent);
                if (transferred > 0) {
                    fprintf(stderr, " partial-transfer=%d", transferred);
                }
                fputc('\n', stderr);
                fputs("Job was NOT retried automatically to avoid duplicate output.\n",
                      stderr);
                close(fd);
                return 1;
            }

            if (transferred == 0) {
                fputs("USB send failed: zero-byte successful transfer.\n",
                      stderr);
                close(fd);
                return 1;
            }
        }
    }

    close(fd);

    if (total_sent != expected_bytes) {
        fprintf(stderr,
                "USB send failed: expected=%lld sent=%lld\n",
                expected_bytes,
                total_sent);
        return 1;
    }

    printf("USB SEND OK\n");
    printf("  endpoint=0x%02x\n", P1102_BULK_OUT);
    printf("  bytes=%lld\n", total_sent);
    return 0;
}

int hplp_print_p1102_test(void)
{
    char zjs_path[128];
    long long bytes = 0;

    if (hplp_render_p1102_test_file(zjs_path,
                                    sizeof(zjs_path),
                                    &bytes) != 0) {
        return 1;
    }

    printf("RENDER OK bytes=%lld\n", bytes);

    libusb_context *context = NULL;
    int rc = libusb_init(&context);
    if (rc != 0) {
        fprintf(stderr, "USB init failed: %d\n", rc);
        unlink(zjs_path);
        return 1;
    }

    int detached = 0;
    libusb_device_handle *handle = open_p1102(context, &detached);

    if (handle == NULL) {
        fputs("Could not open and claim HP LaserJet Pro P1102.\n", stderr);
        libusb_exit(context);
        unlink(zjs_path);
        return 1;
    }

    puts("USB CLAIM OK");
    puts("Sending one A4 test page...");

    rc = send_file(handle, zjs_path, bytes);

    int release_rc = libusb_release_interface(handle, P1102_INTERFACE);
    if (release_rc != 0) {
        fprintf(stderr, "USB release warning: %d\n", release_rc);
    }

    /*
     * P1102 has a known no-reattach quirk. On a real print path we do not
     * force usblp back onto the interface after a completed job. CUPS'
     * libusb backend can continue to access the free interface directly.
     */
    if (detached) {
        puts("kernel-driver-reattach=skipped (P1102 no-reattach quirk)");
    }

    libusb_close(handle);
    libusb_exit(context);
    unlink(zjs_path);

    if (rc != 0) {
        puts("PRINT TEST FAILED");
        return 1;
    }

    puts("PRINT DATA SENT");
    puts("Check whether exactly one test page was printed.");
    return 0;
}

#include "hplp/usb_claim.h"
#include "hplp/models.h"

#include <libusb-1.0/libusb.h>
#include <stdio.h>

static int check_one(libusb_device *device,
                     const struct libusb_device_descriptor *descriptor,
                     const hplp_model_t *model)
{
    libusb_device_handle *handle = NULL;
    int interface_number = model->printer_interface;
    int detached = 0;
    int claimed = 0;
    int rc;

    if (interface_number < 0) {
        printf("SKIP %s: USB printer interface is not verified yet.\n",
               model->model);
        return 0;
    }

    printf("DEVICE %s %04x:%04x interface=%d\n",
           model->model,
           descriptor->idVendor,
           descriptor->idProduct,
           interface_number);

    rc = libusb_open(device, &handle);
    if (rc != 0) {
        printf("  OPEN FAIL error=%d\n", rc);
        return 1;
    }

    rc = libusb_kernel_driver_active(handle, interface_number);
    if (rc == 1) {
        puts("  kernel-driver=active");
        rc = libusb_detach_kernel_driver(handle, interface_number);
        if (rc != 0) {
            printf("  DETACH FAIL error=%d\n", rc);
            libusb_close(handle);
            return 1;
        }
        detached = 1;
        puts("  detach=ok");
    } else if (rc == 0) {
        puts("  kernel-driver=inactive");
    } else if (rc == LIBUSB_ERROR_NOT_SUPPORTED) {
        puts("  kernel-driver=not-supported");
    } else {
        printf("  kernel-driver-check=fail error=%d\n", rc);
        libusb_close(handle);
        return 1;
    }

    rc = libusb_claim_interface(handle, interface_number);
    if (rc != 0) {
        printf("  CLAIM FAIL error=%d\n", rc);
        goto cleanup;
    }

    claimed = 1;
    puts("  claim=ok");

cleanup:
    if (claimed) {
        int release_rc =
            libusb_release_interface(handle, interface_number);
        printf("  release=%s",
               release_rc == 0 ? "ok" : "fail");
        if (release_rc != 0) {
            printf(" error=%d", release_rc);
        }
        putchar('\n');
    }

    /*
     * This is a diagnostic only and sends no printer data. Restore the
     * original kernel-driver state so the existing CUPS setup is left as
     * we found it. The real transport will honor model->no_reattach after
     * actual print jobs.
     */
    if (detached) {
        int attach_rc =
            libusb_attach_kernel_driver(handle, interface_number);
        printf("  restore-kernel-driver=%s",
               attach_rc == 0 ? "ok" : "fail");
        if (attach_rc != 0) {
            printf(" error=%d", attach_rc);
        }
        putchar('\n');
    }

    libusb_close(handle);
    return rc == 0 ? 0 : 1;
}

int hplp_usb_claim_check_supported(void)
{
    libusb_context *context = NULL;
    libusb_device **list = NULL;
    ssize_t total;
    int found = 0;
    int failed = 0;

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
        if (check_one(list[i], &descriptor, model) != 0) {
            failed = 1;
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(context);

    if (!found) {
        puts("No supported HP LaserJet printer found.");
    }

    return failed;
}

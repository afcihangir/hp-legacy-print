#include "hplp/usb_inspect.h"
#include "hplp/models.h"

#include <libusb-1.0/libusb.h>
#include <stdio.h>

static const char *transfer_type_name(uint8_t attributes)
{
    switch (attributes & LIBUSB_TRANSFER_TYPE_MASK) {
    case LIBUSB_TRANSFER_TYPE_CONTROL:
        return "control";
    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
        return "isochronous";
    case LIBUSB_TRANSFER_TYPE_BULK:
        return "bulk";
    case LIBUSB_TRANSFER_TYPE_INTERRUPT:
        return "interrupt";
    default:
        return "unknown";
    }
}

int hplp_usb_inspect_supported(void)
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

        printf("DEVICE %s %04x:%04x bus=%u address=%u\n",
               model->model,
               descriptor.idVendor,
               descriptor.idProduct,
               libusb_get_bus_number(list[i]),
               libusb_get_device_address(list[i]));

        struct libusb_config_descriptor *config = NULL;
        rc = libusb_get_active_config_descriptor(list[i], &config);
        if (rc != 0) {
            printf("  active-config: unavailable error=%d\n", rc);
            continue;
        }

        libusb_device_handle *handle = NULL;
        int open_rc = libusb_open(list[i], &handle);

        printf("  configuration=%u interfaces=%u\n",
               config->bConfigurationValue,
               config->bNumInterfaces);

        for (uint8_t iface_index = 0;
             iface_index < config->bNumInterfaces;
             ++iface_index) {
            const struct libusb_interface *iface =
                &config->interface[iface_index];

            for (int alt_index = 0;
                 alt_index < iface->num_altsetting;
                 ++alt_index) {
                const struct libusb_interface_descriptor *alt =
                    &iface->altsetting[alt_index];

                printf("  interface=%u alt=%u class=%u subclass=%u protocol=%u endpoints=%u",
                       alt->bInterfaceNumber,
                       alt->bAlternateSetting,
                       alt->bInterfaceClass,
                       alt->bInterfaceSubClass,
                       alt->bInterfaceProtocol,
                       alt->bNumEndpoints);

                if (open_rc == 0) {
                    int active =
                        libusb_kernel_driver_active(handle,
                                                    alt->bInterfaceNumber);
                    if (active >= 0) {
                        printf(" kernel-driver=%s",
                               active ? "active" : "inactive");
                    } else {
                        printf(" kernel-driver=unknown(%d)", active);
                    }
                } else {
                    printf(" kernel-driver=unknown(open=%d)", open_rc);
                }

                putchar('\n');

                for (uint8_t endpoint_index = 0;
                     endpoint_index < alt->bNumEndpoints;
                     ++endpoint_index) {
                    const struct libusb_endpoint_descriptor *ep =
                        &alt->endpoint[endpoint_index];

                    printf("    endpoint=0x%02x direction=%s type=%s max-packet=%u\n",
                           ep->bEndpointAddress,
                           (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                   LIBUSB_ENDPOINT_IN
                               ? "in"
                               : "out",
                           transfer_type_name(ep->bmAttributes),
                           ep->wMaxPacketSize);
                }
            }
        }

        if (handle != NULL) {
            libusb_close(handle);
        }

        libusb_free_config_descriptor(config);
    }

    libusb_free_device_list(list, 1);
    libusb_exit(context);

    if (!found) {
        puts("No supported HP LaserJet printer found.");
    }

    return 0;
}

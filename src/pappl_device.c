#define _POSIX_C_SOURCE 200809L

#include "hplp/pappl_device.h"
#include "hplp/usb_discovery.h"

#include <pappl/pappl.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void uri_encode_component(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;

    if (!output_size)
        return;

    while (*input && out + 1 < output_size) {
        unsigned char ch = (unsigned char)*input++;

        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output[out++] = (char)ch;
        } else if (out + 3 < output_size) {
            output[out++] = '%';
            output[out++] = hex[ch >> 4];
            output[out++] = hex[ch & 0x0f];
        } else {
            break;
        }
    }

    output[out] = '\0';
}

static bool hplp_device_list(pappl_device_cb_t cb,
                             void *data,
                             pappl_deverror_cb_t err_cb,
                             void *err_data)
{
    hplp_usb_device_t *devices = NULL;
    size_t count = 0;
    bool stop = false;
    int rc = hplp_usb_list(&devices, &count);

    if (rc != 0) {
        if (err_cb) {
            char message[128];
            snprintf(message, sizeof(message),
                     "HP Legacy Print USB discovery failed: %d", rc);
            err_cb(message, err_data);
        }
        return false;
    }

    for (size_t i = 0; i < count && !stop; ++i) {
        const hplp_usb_device_t *usb = &devices[i];
        char serial[768];
        char uri[1024];
        char device_id[512];

        /*
         * Expose only devices whose transport profile is verified.  This keeps
         * unverified XQX-family entries out of PAPPL until their interface and
         * endpoints have been confirmed on real hardware.
         */
        if (!usb->model ||
            usb->model->printer_interface < 0 ||
            usb->model->bulk_out_endpoint < 0 ||
            usb->serial_state != HPLP_SERIAL_READY ||
            !usb->serial[0]) {
            continue;
        }

        uri_encode_component(usb->serial, serial, sizeof(serial));

        snprintf(uri, sizeof(uri),
                 "hplp://%04x/%04x?serial=%s",
                 usb->vendor_id, usb->product_id, serial);

        snprintf(device_id, sizeof(device_id),
                 "MFG:Hewlett-Packard;MDL:%s;SN:%s;CMD:ZJS;",
                 usb->model->model, usb->serial);

        if (cb)
            stop = cb(usb->model->model, uri, device_id, data);
    }

    hplp_usb_list_free(devices);
    return stop;
}

static bool hplp_device_open(pappl_device_t *device,
                             const char *device_uri,
                             const char *name)
{
    (void)device_uri;
    (void)name;

    /*
     * Discovery is intentionally enabled before transport ownership.  Refuse
     * opens until the persistent libusb session/reconnect layer is attached,
     * so selecting an hplp:// URI cannot accidentally send data yet.
     */
    papplDeviceError(device,
                     "hplp transport is registered for discovery only; printing is not enabled yet.");
    return false;
}

static void hplp_device_close(pappl_device_t *device)
{
    papplDeviceSetData(device, NULL);
}

static ssize_t hplp_device_write(pappl_device_t *device,
                                 const void *buffer,
                                 size_t bytes)
{
    (void)buffer;
    (void)bytes;

    papplDeviceError(device,
                     "hplp transport write attempted before transport activation.");
    return -1;
}

void hplp_pappl_register_device_scheme(void)
{
    /*
     * PAPPL adds its built-in schemes lazily. Initialize those first so adding
     * hplp does not suppress usb/socket/file discovery.
     */
    papplDeviceList((pappl_devtype_t)0, NULL, NULL, NULL, NULL);

    papplDeviceAddScheme("hplp",
                         PAPPL_DEVTYPE_CUSTOM_LOCAL,
                         hplp_device_list,
                         hplp_device_open,
                         hplp_device_close,
                         NULL,
                         hplp_device_write,
                         NULL,
                         NULL);
}

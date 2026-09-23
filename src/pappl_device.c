#define _POSIX_C_SOURCE 200809L

#include "hplp/pappl_device.h"
#include "hplp/usb_discovery.h"
#include "hplp/usb_transport.h"

#include <pappl/pappl.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static bool uri_decode_component(const char *input,
                                 char *output,
                                 size_t output_size)
{
    size_t out = 0;

    if (!input || !output || output_size == 0)
        return false;

    while (*input) {
        unsigned char ch;

        if (out + 1 >= output_size)
            return false;

        if (*input == '%') {
            int hi;
            int lo;

            if (!input[1] || !input[2])
                return false;

            hi = hex_value((unsigned char)input[1]);
            lo = hex_value((unsigned char)input[2]);
            if (hi < 0 || lo < 0)
                return false;

            ch = (unsigned char)((hi << 4) | lo);
            if (ch == 0)
                return false;

            input += 3;
        } else {
            ch = (unsigned char)*input++;
        }

        output[out++] = (char)ch;
    }

    output[out] = '\0';
    return out > 0;
}

static bool parse_hplp_uri(const char *device_uri,
                           uint16_t *vendor_id,
                           uint16_t *product_id,
                           char *serial,
                           size_t serial_size)
{
    unsigned int vendor;
    unsigned int product;
    char encoded_serial[768];
    int consumed = 0;

    if (!device_uri || !vendor_id || !product_id || !serial)
        return false;

    if (sscanf(device_uri,
               "hplp://%4x/%4x?serial=%767s%n",
               &vendor,
               &product,
               encoded_serial,
               &consumed) != 3) {
        return false;
    }

    if (device_uri[consumed] != '\0' ||
        vendor > 0xffffu ||
        product > 0xffffu) {
        return false;
    }

    if (!uri_decode_component(encoded_serial, serial, serial_size))
        return false;

    *vendor_id = (uint16_t)vendor;
    *product_id = (uint16_t)product;
    return true;
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
         * Expose only devices whose transport profile is verified. This keeps
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
    hplp_usb_session_t *session = NULL;
    uint16_t vendor_id;
    uint16_t product_id;
    char serial[256];
    int rc;

    (void)name;

    if (!parse_hplp_uri(device_uri,
                        &vendor_id,
                        &product_id,
                        serial,
                        sizeof(serial))) {
        papplDeviceError(device,
                         "Invalid hplp device URI '%s'.",
                         device_uri ? device_uri : "(null)");
        return false;
    }

    rc = hplp_usb_session_open(vendor_id,
                               product_id,
                               serial,
                               &session);
    if (rc != 0) {
        papplDeviceError(device,
                         "Unable to open hplp USB device %04x:%04x serial=%s: %s",
                         vendor_id,
                         product_id,
                         serial,
                         hplp_usb_error_string(rc));
        return false;
    }

    papplDeviceSetData(device, session);
    return true;
}

static void hplp_device_close(pappl_device_t *device)
{
    hplp_usb_session_t *session =
        (hplp_usb_session_t *)papplDeviceGetData(device);

    papplDeviceSetData(device, NULL);
    hplp_usb_session_close(session);
}

static ssize_t hplp_device_write(pappl_device_t *device,
                                 const void *buffer,
                                 size_t bytes)
{
    hplp_usb_session_t *session =
        (hplp_usb_session_t *)papplDeviceGetData(device);
    ssize_t written;

    if (!session) {
        papplDeviceError(device,
                         "hplp USB write attempted without an open transport session.");
        return -1;
    }

    written = hplp_usb_session_write(session, buffer, bytes);
    if (written < 0) {
        size_t accepted = hplp_usb_session_bytes_accepted(session);
        int error_code = hplp_usb_session_last_error(session);

        papplDeviceError(
            device,
            "hplp USB write failed after %zu byte(s) accepted in this session: %s. "
            "The job will not be replayed automatically.",
            accepted,
            hplp_usb_error_string(error_code)
        );
        return -1;
    }

    return written;
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

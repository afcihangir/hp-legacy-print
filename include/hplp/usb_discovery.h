#ifndef HPLP_USB_DISCOVERY_H
#define HPLP_USB_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>

#include "hplp/models.h"

typedef struct {
    uint8_t bus_number;
    uint8_t device_address;
    uint16_t vendor_id;
    uint16_t product_id;
    char serial[256];
    const hplp_model_t *model;
} hplp_usb_device_t;

int hplp_usb_list(hplp_usb_device_t **devices, size_t *count);
void hplp_usb_list_free(hplp_usb_device_t *devices);

#endif

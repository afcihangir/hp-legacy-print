#ifndef HPLP_MODELS_H
#define HPLP_MODELS_H

#include <stdint.h>

typedef enum {
    HPLP_PROTOCOL_ZJS_Z2 = 1,
    HPLP_PROTOCOL_XQX = 2
} hplp_protocol_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    const char *model;
    hplp_protocol_t protocol;
    int firmware_required;
    const char *firmware_alias;

    /*
     * Verified USB transport metadata.
     * -1 means not yet verified on real hardware.
     */
    int printer_interface;
    int bulk_out_endpoint;
    int bulk_in_endpoint;

    /*
     * Some printers must not have usblp reattached after a job.
     * P1102 is a known example in CUPS' USB quirks table.
     */
    int no_reattach;
} hplp_model_t;

const hplp_model_t *hplp_model_lookup(uint16_t vendor_id, uint16_t product_id);
const char *hplp_protocol_name(hplp_protocol_t protocol);

#endif

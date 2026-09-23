#include "hplp/models.h"

#include <stddef.h>

static const hplp_model_t MODELS[] = {
    {0x03f0, 0x002a, "HP LaserJet Pro P1102", HPLP_PROTOCOL_ZJS_Z2, 0, NULL},
    {0x03f0, 0x3e17, "HP LaserJet P1006", HPLP_PROTOCOL_XQX, 1, "P1006"},
    {0x03f0, 0x3d17, "HP LaserJet P1005", HPLP_PROTOCOL_XQX, 1, "P1005"},
    {0x03f0, 0x4817, "HP LaserJet P1007", HPLP_PROTOCOL_XQX, 1, "P1005"},
    {0x03f0, 0x4917, "HP LaserJet P1008", HPLP_PROTOCOL_XQX, 1, "P1006"},
    {0x03f0, 0x3f17, "HP LaserJet P1505", HPLP_PROTOCOL_XQX, 1, "P1505"},
};

const hplp_model_t *hplp_model_lookup(uint16_t vendor_id, uint16_t product_id)
{
    size_t i;

    for (i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); ++i) {
        if (MODELS[i].vendor_id == vendor_id &&
            MODELS[i].product_id == product_id) {
            return &MODELS[i];
        }
    }

    return NULL;
}

const char *hplp_protocol_name(hplp_protocol_t protocol)
{
    switch (protocol) {
    case HPLP_PROTOCOL_ZJS_Z2:
        return "ZJS/Z2";
    case HPLP_PROTOCOL_XQX:
        return "XQX";
    default:
        return "unknown";
    }
}

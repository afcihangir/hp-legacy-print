#include "hplp/models.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const hplp_model_t *p1102 = hplp_model_lookup(0x03f0, 0x002a);
    assert(p1102 != NULL);
    assert(strcmp(p1102->model, "HP LaserJet Pro P1102") == 0);
    assert(p1102->protocol == HPLP_PROTOCOL_ZJS_Z2);
    assert(p1102->firmware_required == 0);
    assert(p1102->printer_interface == 0);
    assert(p1102->bulk_out_endpoint == 0x01);
    assert(p1102->bulk_in_endpoint == 0x81);
    assert(p1102->no_reattach == 1);

    const hplp_model_t *p1006 = hplp_model_lookup(0x03f0, 0x3e17);
    assert(p1006 != NULL);
    assert(p1006->protocol == HPLP_PROTOCOL_XQX);
    assert(p1006->firmware_required == 1);
    assert(strcmp(p1006->firmware_alias, "P1006") == 0);
    assert(p1006->printer_interface == -1);
    assert(p1006->bulk_out_endpoint == -1);
    assert(p1006->bulk_in_endpoint == -1);

    assert(hplp_model_lookup(0xffff, 0xffff) == NULL);

    return 0;
}

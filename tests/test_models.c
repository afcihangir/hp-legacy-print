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

    const hplp_model_t *p1006 = hplp_model_lookup(0x03f0, 0x3e17);
    assert(p1006 != NULL);
    assert(p1006->protocol == HPLP_PROTOCOL_XQX);
    assert(p1006->firmware_required == 1);
    assert(strcmp(p1006->firmware_alias, "P1006") == 0);

    assert(hplp_model_lookup(0xffff, 0xffff) == NULL);

    return 0;
}

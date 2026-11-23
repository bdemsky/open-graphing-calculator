/*
 * The advanced CAS extension as a code module (app/modx.h; docs/QSPI_CONTENT.md
 * 12.6). The code is cas/ext/adv (number theory, combinatorics, list
 * statistics, matrix functions, Jordan form, minpoly, rsolve); this is only
 * its modx entry: init gets the CAS extension table from the modx API and
 * registers the commands with the CAS. The module has no RW state and no
 * ops beyond the mandatory size: the CAS calls the registered handlers.
 *
 * The core binds it on the first use of one of its commands (casx.c asks
 * the loader modx.c installs) and forgets the registration at every store
 * change (modx_unbind_all -> cas_ext_unload).
 */
#include <stddef.h>
#include "modx.h"

struct casx_api;
int casx_adv_init(const struct casx_api *X); /* cas/ext/adv/adv.c */
int casx_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);

int casx_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    (void)rw;
    if (api->abi != MODX_ABI || api->size < offsetof(modx_api_t, casx_api) + sizeof api->casx_api || !api->casx_api)
        return -1;
    const struct casx_api *X = api->casx_api();
    if (!X)
        return -2;
    ops->size = sizeof *ops;
    return casx_adv_init(X) ? -3 : 0;
}

MODX_DESCRIPTOR(CASX_MODULE, 0, casx_module_init);

/* tesseraOS stubs. tessera has no IPC endpoints or framebuffer yet (see its docs/ROADMAP.md),
 * so these report TEDS_ERR_UNSUPPORTED. The teds server and client do not depend on them. */
#include "teds/tessera.h"

int teds_tessera_fb_open(teds_fb *fb) {
    (void)fb;
    return TEDS_ERR_UNSUPPORTED;
}

int teds_tessera_transport_open(teds_transport *t, uint32_t endpoint) {
    (void)t;
    (void)endpoint;
    return TEDS_ERR_UNSUPPORTED;
}

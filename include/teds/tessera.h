/* tesseraOS backends. These are stubs until tessera has IPC endpoints and a framebuffer. */
#ifndef TEDS_TESSERA_H
#define TEDS_TESSERA_H

#include "teds/server.h"
#include "teds/teds.h"

/* Fill fb from the boot framebuffer (GOP). Returns TEDS_ERR_UNSUPPORTED until tessera provides it. */
int teds_tessera_fb_open(teds_fb *fb);

/* Make a transport over an IPC endpoint held by this task. Returns TEDS_ERR_UNSUPPORTED for now. */
int teds_tessera_transport_open(teds_transport *t, uint32_t endpoint);

#endif

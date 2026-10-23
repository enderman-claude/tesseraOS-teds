/* teds server API. The display service links this and runs one instance per screen. */
#ifndef TEDS_SERVER_H
#define TEDS_SERVER_H

#include <stdint.h>
#include "teds/protocol.h"
#include "teds/teds.h"

#define TEDS_MAX_CLIENTS 8
#define TEDS_MAX_SURFACES 32

/* Framebuffer as the server sees it. Backends fill this in. */
typedef struct teds_fb {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
} teds_fb;

typedef struct teds_server teds_server;

/* Start a server on a framebuffer. The server does not own the framebuffer memory. */
int teds_server_init(teds_server **out, const teds_fb *fb);
void teds_server_free(teds_server *s);

/* Attach a client by its server-side transport. Returns a client slot, or a negative
 * status. Each attached transport is served by teds_server_poll. */
int teds_server_attach(teds_server *s, const teds_transport *t, uint32_t *client_slot_out);
void teds_server_detach(teds_server *s, uint32_t client_slot);

/* Handle every pending request, then composite if anything changed. Returns the number of
 * requests handled. */
int teds_server_poll(teds_server *s);

/* Composite all surfaces onto the framebuffer now. Returns 0 if direct mode is active. */
int teds_server_composite(teds_server *s);

/* Introspection for tests and tools. */
int teds_server_mode(const teds_server *s);            /* 0 composite, 1 direct */
int teds_server_direct_owner(const teds_server *s);    /* client slot, or -1 */
uint32_t teds_server_surface_count(const teds_server *s);

enum teds_mode { TEDS_MODE_COMPOSITE = 0, TEDS_MODE_DIRECT = 1 };

#endif

/* teds client API. Apps include this header. */
#ifndef TEDS_H
#define TEDS_H

#include <stdint.h>
#include "teds/protocol.h"

typedef struct teds_transport teds_transport;
typedef struct teds_conn teds_conn;
typedef struct teds_surface teds_surface;

/* A transport moves teds_msg values between a client and teds. `kick` lets an in-process
 * transport run the server; a real IPC transport leaves it empty because teds is its
 * own task. */
struct teds_transport {
    int (*send)(void *ctx, const teds_msg *msg);
    int (*recv)(void *ctx, teds_msg *msg);
    void (*kick)(void *ctx);
    void *ctx;
};

/* Direct mode grant: a framebuffer the app can draw into until it releases it. */
typedef struct teds_direct_fb {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;              /* in pixels, not bytes */
    uint32_t format;             /* 0 = 0x00RRGGBB */
} teds_direct_fb;

/* Connect with the given capability bits (TEDS_CAP_*). */
int teds_connect(teds_conn **out, const teds_transport *t, uint32_t caps);
void teds_disconnect(teds_conn *c);

/* Client mode. The surface owns a w*h buffer of 0x00RRGGBB pixels. */
int teds_surface_create(teds_conn *c, uint32_t w, uint32_t h, int32_t x, int32_t y,
                        teds_surface **out);
uint32_t *teds_surface_pixels(teds_surface *s);
uint32_t teds_surface_id(const teds_surface *s);
int teds_surface_commit(teds_surface *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* Destroying removes the surface from the server. teds_disconnect frees any surfaces still
 * open, so their handles must not be used after it. */
int teds_surface_destroy(teds_surface *s);

/* Direct mode. Requires TEDS_CAP_DIRECT at connect time. */
int teds_direct_acquire(teds_conn *c, teds_direct_fb *fb);
int teds_direct_release(teds_conn *c);

#endif

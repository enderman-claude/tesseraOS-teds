/* teds client library. One connection per app. Calls are synchronous: each sends a request
 * and waits for its reply over the transport. */
#include <stdlib.h>
#include <string.h>

#include "teds/teds.h"

struct teds_surface {
    teds_conn *c;
    uint32_t id;
    uint32_t w, h;
    uint32_t *pixels;
    teds_surface *next;    /* the connection's list of open surfaces */
};

struct teds_conn {
    teds_transport t;
    uint32_t serial;
    uint32_t client_id;
    uint32_t caps;
    teds_surface *surfaces;
};

static int request(teds_conn *c, teds_msg *req, teds_msg *rep) {
    req->serial = ++c->serial;
    if (c->t.send(c->t.ctx, req) != 0) {
        return TEDS_ERR_PROTOCOL;
    }
    if (c->t.kick != NULL) {
        c->t.kick(c->t.ctx);
    }
    if (c->t.recv(c->t.ctx, rep) != 1 || rep->serial != req->serial) {
        return TEDS_ERR_PROTOCOL;
    }
    if (rep->op == TEDS_OP_REPLY) {
        return rep->status;
    }
    return TEDS_OK;
}

int teds_connect(teds_conn **out, const teds_transport *t, uint32_t caps) {
    teds_conn *c = calloc(1, sizeof *c);
    if (c == NULL) {
        return TEDS_ERR_NOMEM;
    }
    c->t = *t;
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_HELLO;
    req.u.hello.version = TEDS_PROTOCOL_VERSION;
    req.u.hello.caps = caps;
    int st = request(c, &req, &rep);
    if (st != TEDS_OK || rep.op != TEDS_OP_HELLO_REPLY) {
        free(c);
        return st != TEDS_OK ? st : TEDS_ERR_PROTOCOL;
    }
    c->client_id = rep.u.hello_reply.client_id;
    c->caps = rep.u.hello_reply.caps;
    *out = c;
    return TEDS_OK;
}

void teds_disconnect(teds_conn *c) {
    if (c == NULL) {
        return;
    }
    /* Surfaces the app did not destroy are freed here, so their handles are invalid after this. */
    while (c->surfaces != NULL) {
        teds_surface *s = c->surfaces;
        c->surfaces = s->next;
        free(s->pixels);
        free(s);
    }
    /* GOODBYE gets no reply: the server drops the client and releases its surfaces. */
    teds_msg msg;
    memset(&msg, 0, sizeof msg);
    msg.op = TEDS_OP_GOODBYE;
    msg.serial = ++c->serial;
    c->t.send(c->t.ctx, &msg);
    if (c->t.kick != NULL) {
        c->t.kick(c->t.ctx);
    }
    free(c);
}

int teds_surface_create(teds_conn *c, uint32_t w, uint32_t h, int32_t x, int32_t y,
                        teds_surface **out) {
    if (w == 0 || h == 0) {
        return TEDS_ERR_INVALID;
    }
    uint32_t *pixels = calloc((size_t)w * h, sizeof *pixels);
    if (pixels == NULL) {
        return TEDS_ERR_NOMEM;
    }
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_CREATE_SURFACE;
    req.u.create.x = x;
    req.u.create.y = y;
    req.u.create.w = w;
    req.u.create.h = h;
    req.u.create.mem = (uint64_t)(uintptr_t)pixels;
    int st = request(c, &req, &rep);
    if (st != TEDS_OK) {
        free(pixels);
        return st;
    }
    teds_surface *s = calloc(1, sizeof *s);
    if (s == NULL) {
        free(pixels);
        return TEDS_ERR_NOMEM;
    }
    s->c = c;
    s->id = rep.surface;
    s->w = w;
    s->h = h;
    s->pixels = pixels;
    s->next = c->surfaces;
    c->surfaces = s;
    *out = s;
    return TEDS_OK;
}

uint32_t *teds_surface_pixels(teds_surface *s) {
    return s->pixels;
}

uint32_t teds_surface_id(const teds_surface *s) {
    return s->id;
}

int teds_surface_commit(teds_surface *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (x > s->w || y > s->h || w > s->w - x || h > s->h - y) {
        return TEDS_ERR_INVALID;
    }
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_COMMIT;
    req.surface = s->id;
    req.u.commit.x = x;
    req.u.commit.y = y;
    req.u.commit.w = w;
    req.u.commit.h = h;
    return request(s->c, &req, &rep);
}

int teds_surface_destroy(teds_surface *s) {
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_DESTROY_SURFACE;
    req.surface = s->id;
    int st = request(s->c, &req, &rep);
    if (st == TEDS_OK) {
        teds_surface **link = &s->c->surfaces;
        while (*link != NULL && *link != s) {
            link = &(*link)->next;
        }
        if (*link == s) {
            *link = s->next;
        }
        free(s->pixels);
        free(s);
    }
    return st;
}

int teds_direct_acquire(teds_conn *c, teds_direct_fb *fb) {
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_DIRECT_REQUEST;
    int st = request(c, &req, &rep);
    if (st != TEDS_OK) {
        return st;
    }
    fb->pixels = (uint32_t *)(uintptr_t)rep.u.direct.mem;
    fb->width = rep.u.direct.w;
    fb->height = rep.u.direct.h;
    fb->pitch = rep.u.direct.pitch;
    fb->format = rep.u.direct.format;
    return TEDS_OK;
}

int teds_direct_release(teds_conn *c) {
    teds_msg req, rep;
    memset(&req, 0, sizeof req);
    req.op = TEDS_OP_DIRECT_RELEASE;
    return request(c, &req, &rep);
}

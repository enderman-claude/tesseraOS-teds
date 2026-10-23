/* teds server: client table, surfaces, direct mode and compositing. Single-threaded. The
 * caller decides when to poll; in tesseraOS this is the service main loop. */
#include <stdlib.h>
#include <string.h>

#include "teds/server.h"

#define TEDS_BACKGROUND 0x101820u
#define TEDS_MAX_DIM 4096u

typedef struct cli {
    int used;
    int hello;
    teds_transport t;
    uint32_t allowed;
    uint32_t granted;
    uint32_t id;
} cli;

typedef struct surf {
    int used;
    uint32_t id;
    uint32_t owner;        /* client slot */
    int32_t x, y;
    uint32_t w, h;
    uint32_t z;            /* creation order; higher is on top */
    uint32_t *pixels;
} surf;

struct teds_server {
    teds_fb fb;
    int mode;
    int owner;             /* direct mode holder slot, or -1 */
    int dirty;
    uint32_t next_surface_id;
    uint32_t next_client_id;
    uint32_t next_z;
    cli clients[TEDS_MAX_CLIENTS];
    surf surfaces[TEDS_MAX_SURFACES];
};

int teds_server_init(teds_server **out, const teds_fb *fb) {
    if (fb == NULL || fb->pixels == NULL || fb->width == 0 || fb->height == 0 ||
        fb->pitch < fb->width) {
        return TEDS_ERR_INVALID;
    }
    teds_server *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return TEDS_ERR_NOMEM;
    }
    s->fb = *fb;
    s->owner = -1;
    s->next_surface_id = 1;
    s->next_client_id = 1;
    s->next_z = 1;
    s->dirty = 1;
    *out = s;
    return TEDS_OK;
}

void teds_server_free(teds_server *s) {
    if (s == NULL) {
        return;
    }
    /* Surface pixels belong to their clients. The server only drops its references. */
    free(s);
}

int teds_server_attach(teds_server *s, const teds_transport *t, uint32_t allowed_caps,
                       uint32_t *client_slot_out) {
    for (uint32_t i = 0; i < TEDS_MAX_CLIENTS; i++) {
        if (!s->clients[i].used) {
            memset(&s->clients[i], 0, sizeof s->clients[i]);
            s->clients[i].used = 1;
            s->clients[i].t = *t;
            s->clients[i].allowed = allowed_caps;
            *client_slot_out = i;
            return TEDS_OK;
        }
    }
    return TEDS_ERR_NOMEM;
}

static void drop_client(teds_server *s, uint32_t slot) {
    if (s->owner == (int)slot) {
        s->mode = TEDS_MODE_COMPOSITE;
        s->owner = -1;
    }
    for (uint32_t i = 0; i < TEDS_MAX_SURFACES; i++) {
        if (s->surfaces[i].used && s->surfaces[i].owner == slot) {
            s->surfaces[i].used = 0;
        }
    }
    memset(&s->clients[slot], 0, sizeof s->clients[slot]);
    s->dirty = 1;
}

void teds_server_detach(teds_server *s, uint32_t client_slot) {
    if (client_slot < TEDS_MAX_CLIENTS && s->clients[client_slot].used) {
        drop_client(s, client_slot);
    }
}

static surf *find_surface(teds_server *s, uint32_t id) {
    if (id == 0) {
        return NULL;
    }
    for (uint32_t i = 0; i < TEDS_MAX_SURFACES; i++) {
        if (s->surfaces[i].used && s->surfaces[i].id == id) {
            return &s->surfaces[i];
        }
    }
    return NULL;
}

static void make_reply(teds_msg *rep, const teds_msg *req, int32_t status) {
    memset(rep, 0, sizeof *rep);
    rep->op = TEDS_OP_REPLY;
    rep->serial = req->serial;
    rep->surface = req->surface;
    rep->status = status;
}

static void handle(teds_server *s, uint32_t slot, const teds_msg *req, teds_msg *rep) {
    cli *c = &s->clients[slot];
    make_reply(rep, req, TEDS_OK);

    if (req->op == TEDS_OP_HELLO) {
        if (req->u.hello.version != TEDS_PROTOCOL_VERSION) {
            make_reply(rep, req, TEDS_ERR_INVALID);
            return;
        }
        c->hello = 1;
        c->granted = req->u.hello.caps & c->allowed;
        c->id = s->next_client_id++;
        rep->op = TEDS_OP_HELLO_REPLY;
        rep->u.hello_reply.client_id = c->id;
        rep->u.hello_reply.caps = c->granted;
        return;
    }
    if (req->op == TEDS_OP_GOODBYE) {
        drop_client(s, slot);
        return;
    }
    if (!c->hello) {
        make_reply(rep, req, TEDS_ERR_INVALID);
        return;
    }

    switch (req->op) {
    case TEDS_OP_CREATE_SURFACE: {
        uint32_t w = req->u.create.w, h = req->u.create.h;
        if (!(c->granted & TEDS_CAP_CLIENT)) {
            make_reply(rep, req, TEDS_ERR_DENIED);
            return;
        }
        if (w == 0 || h == 0 || w > TEDS_MAX_DIM || h > TEDS_MAX_DIM || req->u.create.mem == 0) {
            make_reply(rep, req, TEDS_ERR_INVALID);
            return;
        }
        for (uint32_t i = 0; i < TEDS_MAX_SURFACES; i++) {
            if (!s->surfaces[i].used) {
                surf *f = &s->surfaces[i];
                memset(f, 0, sizeof *f);
                f->used = 1;
                f->id = s->next_surface_id++;
                f->owner = slot;
                f->x = req->u.create.x;
                f->y = req->u.create.y;
                f->w = w;
                f->h = h;
                f->z = s->next_z++;
                f->pixels = (uint32_t *)(uintptr_t)req->u.create.mem;
                rep->surface = f->id;
                s->dirty = 1;
                return;
            }
        }
        make_reply(rep, req, TEDS_ERR_NOMEM);
        return;
    }
    case TEDS_OP_COMMIT: {
        surf *f = find_surface(s, req->surface);
        if (f == NULL || f->owner != slot) {
            make_reply(rep, req, TEDS_ERR_INVALID);
            return;
        }
        /* A commit only marks the surface dirty. Compositing runs after the poll. */
        s->dirty = 1;
        return;
    }
    case TEDS_OP_DESTROY_SURFACE: {
        surf *f = find_surface(s, req->surface);
        if (f == NULL || f->owner != slot) {
            make_reply(rep, req, TEDS_ERR_INVALID);
            return;
        }
        f->used = 0;
        s->dirty = 1;
        return;
    }
    case TEDS_OP_DIRECT_REQUEST: {
        if (!(c->granted & TEDS_CAP_DIRECT)) {
            make_reply(rep, req, TEDS_ERR_DENIED);
            return;
        }
        if (s->owner != -1 && s->owner != (int)slot) {
            make_reply(rep, req, TEDS_ERR_BUSY);
            return;
        }
        s->owner = (int)slot;
        s->mode = TEDS_MODE_DIRECT;
        rep->u.direct.w = s->fb.width;
        rep->u.direct.h = s->fb.height;
        rep->u.direct.pitch = s->fb.pitch;
        rep->u.direct.format = s->fb.format;
        rep->u.direct.mem = (uint64_t)(uintptr_t)s->fb.pixels;
        return;
    }
    case TEDS_OP_DIRECT_RELEASE: {
        if (s->owner != (int)slot) {
            make_reply(rep, req, TEDS_ERR_INVALID);
            return;
        }
        s->owner = -1;
        s->mode = TEDS_MODE_COMPOSITE;
        s->dirty = 1;
        return;
    }
    default:
        make_reply(rep, req, TEDS_ERR_PROTOCOL);
        return;
    }
}

int teds_server_poll(teds_server *s) {
    int handled = 0;
    for (uint32_t slot = 0; slot < TEDS_MAX_CLIENTS; slot++) {
        teds_msg req, rep;
        while (s->clients[slot].used && s->clients[slot].t.recv(s->clients[slot].t.ctx, &req) == 1) {
            handle(s, slot, &req, &rep);
            handled++;
            if (s->clients[slot].used) {
                s->clients[slot].t.send(s->clients[slot].t.ctx, &rep);
            }
        }
    }
    if (s->dirty) {
        teds_server_composite(s);
    }
    return handled;
}

static void fill_background(teds_server *s) {
    for (uint32_t y = 0; y < s->fb.height; y++) {
        for (uint32_t x = 0; x < s->fb.width; x++) {
            s->fb.pixels[(size_t)y * s->fb.pitch + x] = TEDS_BACKGROUND;
        }
    }
}

static void draw_surface(teds_server *s, const surf *f) {
    for (uint32_t r = 0; r < f->h; r++) {
        int64_t dy = (int64_t)f->y + r;
        if (dy < 0 || dy >= (int64_t)s->fb.height) {
            continue;
        }
        for (uint32_t c = 0; c < f->w; c++) {
            int64_t dx = (int64_t)f->x + c;
            if (dx < 0 || dx >= (int64_t)s->fb.width) {
                continue;
            }
            s->fb.pixels[(size_t)dy * s->fb.pitch + (size_t)dx] = f->pixels[(size_t)r * f->w + c];
        }
    }
}

int teds_server_composite(teds_server *s) {
    if (s->mode == TEDS_MODE_DIRECT) {
        return 0;
    }
    fill_background(s);
    uint32_t last = 0;
    for (;;) {
        surf *next = NULL;
        for (uint32_t i = 0; i < TEDS_MAX_SURFACES; i++) {
            surf *f = &s->surfaces[i];
            if (f->used && f->z > last && (next == NULL || f->z < next->z)) {
                next = f;
            }
        }
        if (next == NULL) {
            break;
        }
        draw_surface(s, next);
        last = next->z;
    }
    s->dirty = 0;
    return 1;
}

int teds_server_mode(const teds_server *s) {
    return s->mode;
}

int teds_server_direct_owner(const teds_server *s) {
    return s->owner;
}

uint32_t teds_server_surface_count(const teds_server *s) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < TEDS_MAX_SURFACES; i++) {
        n += s->surfaces[i].used ? 1 : 0;
    }
    return n;
}

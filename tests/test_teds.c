/* Host tests for teds over the loopback transport. Exit status 0 means every check passed. */
#include <stdio.h>
#include <stdlib.h>

#include "teds/loopback.h"
#include "teds/server.h"
#include "teds/tessera.h"
#include "teds/teds.h"

static int failures;

#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

#define W 64u
#define H 48u
#define BG 0x101820u

typedef struct fixture {
    uint32_t *screen;
    teds_server *srv;
} fixture;

static void fixture_init(fixture *f) {
    f->screen = calloc((size_t)W * H, sizeof(uint32_t));
    teds_fb fb = { f->screen, W, H, W, 0 };
    CHECK(teds_server_init(&f->srv, &fb) == TEDS_OK, "server init");
}

static void fixture_free(fixture *f) {
    teds_server_free(f->srv);
    free(f->screen);
}

static uint32_t px(fixture *f, uint32_t x, uint32_t y) {
    return f->screen[(size_t)y * W + x];
}

/* Attach a client with the given allowed caps and connect it with the requested caps. */
static teds_conn *connect_client(fixture *f, uint32_t allowed, uint32_t requested,
                                 teds_loop **loop_out) {
    teds_loop *loop = NULL;
    CHECK(teds_loop_create(&loop, f->srv) == TEDS_OK, "loop create");
    uint32_t slot;
    CHECK(teds_server_attach(f->srv, teds_loop_server(loop), allowed, &slot) == TEDS_OK, "attach");
    teds_conn *c = NULL;
    CHECK(teds_connect(&c, teds_loop_client(loop), requested) == TEDS_OK, "connect");
    *loop_out = loop;
    return c;
}

static void test_client_mode_composites_surface(fixture *f) {
    teds_loop *loop;
    teds_conn *c = connect_client(f, TEDS_CAP_CLIENT, TEDS_CAP_CLIENT, &loop);
    CHECK(c != NULL, "client connected");

    teds_surface *s = NULL;
    CHECK(teds_surface_create(c, 8, 8, 10, 5, &s) == TEDS_OK, "surface create");
    uint32_t *p = teds_surface_pixels(s);
    for (uint32_t i = 0; i < 8 * 8; i++) {
        p[i] = 0xFF0000u;
    }
    CHECK(teds_surface_commit(s, 0, 0, 8, 8) == TEDS_OK, "commit");

    CHECK(px(f, 10, 5) == 0xFF0000u, "surface top-left is drawn");
    CHECK(px(f, 17, 12) == 0xFF0000u, "surface bottom-right is drawn");
    CHECK(px(f, 18, 12) == BG, "outside surface is background");
    CHECK(px(f, 9, 5) == BG, "left of surface is background");
    CHECK(teds_server_surface_count(f->srv) == 1, "one surface on the server");

    CHECK(teds_surface_destroy(s) == TEDS_OK, "destroy");
    CHECK(teds_server_surface_count(f->srv) == 0, "destroyed surface is gone");
    CHECK(px(f, 10, 5) == BG, "destroyed surface no longer drawn");

    teds_disconnect(c);
    teds_loop_free(loop);
}

static void test_z_order_and_clipping(fixture *f) {
    teds_loop *loop;
    teds_conn *c = connect_client(f, TEDS_CAP_CLIENT, TEDS_CAP_CLIENT, &loop);

    teds_surface *below = NULL, *above = NULL, *edge = NULL;
    CHECK(teds_surface_create(c, 10, 10, 0, 0, &below) == TEDS_OK, "below create");
    CHECK(teds_surface_create(c, 10, 10, 5, 5, &above) == TEDS_OK, "above create");
    CHECK(teds_surface_create(c, 6, 6, -3, -3, &edge) == TEDS_OK, "edge create");
    for (uint32_t i = 0; i < 100; i++) {
        teds_surface_pixels(below)[i] = 0x0000FFu;
        teds_surface_pixels(above)[i] = 0x00FF00u;
    }
    for (uint32_t i = 0; i < 36; i++) {
        teds_surface_pixels(edge)[i] = 0xFFFFFFu;
    }
    teds_surface_commit(below, 0, 0, 10, 10);
    teds_surface_commit(above, 0, 0, 10, 10);
    teds_surface_commit(edge, 0, 0, 6, 6);

    CHECK(px(f, 2, 2) == 0xFFFFFFu, "edge surface is drawn over the one below");
    CHECK(px(f, 0, 0) == 0xFFFFFFu, "clipped top-left corner of edge surface");
    CHECK(px(f, 3, 3) == 0x0000FFu, "below surface visible outside the overlap");
    CHECK(px(f, 6, 6) == 0x00FF00u, "above surface wins the overlap");
    CHECK(px(f, 14, 14) == 0x00FF00u, "above surface extends past below");
    CHECK(px(f, 15, 15) == BG, "outside both surfaces is background");

    teds_surface_destroy(below);
    teds_surface_destroy(above);
    teds_surface_destroy(edge);
    teds_disconnect(c);
    teds_loop_free(loop);
}

static void test_direct_needs_capability(fixture *f) {
    teds_loop *loop;
    teds_conn *c = connect_client(f, TEDS_CAP_CLIENT, TEDS_CAP_DIRECT, &loop);
    CHECK(c != NULL, "client connected");
    teds_direct_fb fb;
    CHECK(teds_direct_acquire(c, &fb) == TEDS_ERR_DENIED, "direct denied without manifest grant");
    CHECK(teds_server_mode(f->srv) == TEDS_MODE_COMPOSITE, "still composite");
    teds_disconnect(c);
    teds_loop_free(loop);
}

static void test_direct_mode_exclusive(fixture *f) {
    teds_loop *la, *lb;
    teds_conn *a = connect_client(f, TEDS_CAP_CLIENT | TEDS_CAP_DIRECT,
                                  TEDS_CAP_CLIENT | TEDS_CAP_DIRECT, &la);
    teds_conn *b = connect_client(f, TEDS_CAP_CLIENT | TEDS_CAP_DIRECT,
                                  TEDS_CAP_CLIENT | TEDS_CAP_DIRECT, &lb);

    /* A client surface exists before direct mode starts. */
    teds_surface *s = NULL;
    teds_surface_create(b, 4, 4, 0, 0, &s);
    for (uint32_t i = 0; i < 16; i++) {
        teds_surface_pixels(s)[i] = 0xAAAAAAu;
    }
    teds_surface_commit(s, 0, 0, 4, 4);
    CHECK(px(f, 1, 1) == 0xAAAAAAu, "surface drawn before direct mode");

    teds_direct_fb fb;
    CHECK(teds_direct_acquire(a, &fb) == TEDS_OK, "A acquires direct mode");
    CHECK(fb.width == W && fb.height == H && fb.pitch == W, "framebuffer geometry");
    CHECK(fb.pixels == f->screen, "A gets the real framebuffer");
    CHECK(teds_server_mode(f->srv) == TEDS_MODE_DIRECT, "server is in direct mode");
    CHECK(teds_server_direct_owner(f->srv) == 0, "A owns direct mode");

    teds_direct_fb fb2;
    CHECK(teds_direct_acquire(b, &fb2) == TEDS_ERR_BUSY, "B is refused while A holds it");
    CHECK(teds_direct_acquire(a, &fb2) == TEDS_OK, "A can ask again without error");

    /* Compositing is suspended: B's commit must not touch the screen. */
    for (uint32_t i = 0; i < 16; i++) {
        teds_surface_pixels(s)[i] = 0xBBBBBBu;
    }
    teds_surface_commit(s, 0, 0, 4, 4);
    CHECK(px(f, 1, 1) == 0xAAAAAAu, "compositing suspended in direct mode");

    /* A draws an OSD pixel directly. */
    fb.pixels[5 * W + 5] = 0x123456u;
    CHECK(px(f, 5, 5) == 0x123456u, "direct drawing reaches the screen");

    CHECK(teds_direct_release(b) == TEDS_ERR_INVALID, "B cannot release A's direct mode");
    CHECK(teds_direct_release(a) == TEDS_OK, "A releases");
    CHECK(teds_server_mode(f->srv) == TEDS_MODE_COMPOSITE, "composite again after release");
    CHECK(px(f, 1, 1) == 0xBBBBBBu, "composite repaints the client surface");

    teds_surface_destroy(s);
    teds_disconnect(a);
    teds_disconnect(b);
    teds_loop_free(la);
    teds_loop_free(lb);
}

static void test_disconnect_releases_direct_and_surfaces(fixture *f) {
    teds_loop *loop;
    teds_conn *c = connect_client(f, TEDS_CAP_CLIENT | TEDS_CAP_DIRECT,
                                  TEDS_CAP_CLIENT | TEDS_CAP_DIRECT, &loop);
    teds_surface *s = NULL;
    teds_surface_create(c, 4, 4, 0, 0, &s);
    teds_surface_commit(s, 0, 0, 4, 4);
    teds_direct_fb fb;
    CHECK(teds_direct_acquire(c, &fb) == TEDS_OK, "acquire before disconnect");

    teds_disconnect(c);
    teds_server_poll(f->srv);
    CHECK(teds_server_mode(f->srv) == TEDS_MODE_COMPOSITE, "disconnect restores composite");
    CHECK(teds_server_direct_owner(f->srv) == -1, "no direct owner after disconnect");
    CHECK(teds_server_surface_count(f->srv) == 0, "disconnect removes the client surfaces");
    teds_loop_free(loop);
}

static void test_bad_requests(fixture *f) {
    teds_loop *loop;
    teds_conn *c = connect_client(f, TEDS_CAP_CLIENT, TEDS_CAP_CLIENT, &loop);
    teds_surface *s = NULL;
    CHECK(teds_surface_create(c, 0, 4, 0, 0, &s) == TEDS_ERR_INVALID, "zero width rejected");
    CHECK(teds_surface_create(c, 4, 4, 0, 0, &s) == TEDS_OK, "valid create");
    CHECK(teds_surface_commit(s, 3, 0, 4, 4) == TEDS_ERR_INVALID, "damage outside surface rejected");
    teds_surface_destroy(s);

    /* A second client cannot destroy the first client's surface. */
    teds_loop *loop2;
    teds_conn *d = connect_client(f, TEDS_CAP_CLIENT, TEDS_CAP_CLIENT, &loop2);
    teds_surface *mine = NULL;
    teds_surface_create(c, 2, 2, 0, 0, &mine);
    teds_surface_commit(mine, 0, 0, 2, 2);
    /* Send a DESTROY for c's surface straight over d's transport. */
    teds_msg destroy = { 0 };
    destroy.op = TEDS_OP_DESTROY_SURFACE;
    destroy.serial = 99;
    destroy.surface = teds_surface_id(mine);
    const teds_transport *dt = teds_loop_client(loop2);
    CHECK(dt->send(dt->ctx, &destroy) == 0, "send foreign destroy");
    dt->kick(dt->ctx);
    teds_msg rep;
    CHECK(dt->recv(dt->ctx, &rep) == 1 && rep.serial == 99, "foreign destroy got a reply");
    CHECK(rep.op == TEDS_OP_REPLY && rep.status == TEDS_ERR_INVALID,
          "other client cannot destroy it");
    CHECK(teds_server_surface_count(f->srv) == 1, "surface survives the foreign destroy");
    teds_surface_destroy(mine);

    teds_disconnect(d);
    teds_disconnect(c);
    teds_loop_free(loop);
    teds_loop_free(loop2);
}

static void test_version_mismatch_refused(fixture *f) {
    teds_loop *loop;
    CHECK(teds_loop_create(&loop, f->srv) == TEDS_OK, "loop create");
    uint32_t slot;
    teds_server_attach(f->srv, teds_loop_server(loop), TEDS_CAP_CLIENT, &slot);
    /* Hand-built HELLO with the wrong version, sent straight over the loopback. */
    teds_msg hello = { 0 };
    hello.op = TEDS_OP_HELLO;
    hello.serial = 7;
    hello.u.hello.version = TEDS_PROTOCOL_VERSION + 1;
    hello.u.hello.caps = TEDS_CAP_CLIENT;
    const teds_transport *t = teds_loop_client(loop);
    CHECK(t->send(t->ctx, &hello) == 0, "send hello");
    t->kick(t->ctx);
    teds_msg rep;
    CHECK(t->recv(t->ctx, &rep) == 1, "reply received");
    CHECK(rep.op == TEDS_OP_REPLY && rep.status == TEDS_ERR_INVALID && rep.serial == 7,
          "version mismatch replied with INVALID");
    teds_loop_free(loop);
}

static void test_tessera_stubs_report_unsupported(void) {
    teds_fb fb;
    teds_transport t;
    CHECK(teds_tessera_fb_open(&fb) == TEDS_ERR_UNSUPPORTED, "tessera framebuffer stub");
    CHECK(teds_tessera_transport_open(&t, 1) == TEDS_ERR_UNSUPPORTED, "tessera transport stub");
}

int main(void) {
    fixture f;
    fixture_init(&f);
    test_client_mode_composites_surface(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_z_order_and_clipping(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_direct_needs_capability(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_direct_mode_exclusive(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_disconnect_releases_direct_and_surfaces(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_bad_requests(&f);
    fixture_free(&f);

    fixture_init(&f);
    test_version_mismatch_refused(&f);
    fixture_free(&f);

    test_tessera_stubs_report_unsupported();

    if (failures) {
        fprintf(stderr, "test_teds: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_teds: all checks passed\n");
    return 0;
}

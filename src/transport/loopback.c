#include <stdlib.h>
#include <string.h>

#include "teds/loopback.h"

#define QUEUE_LEN 32

typedef struct queue {
    teds_msg msg[QUEUE_LEN];
    unsigned head;
    unsigned count;
} queue;

typedef struct end {
    struct teds_loop *loop;
    int is_client;
} end;

struct teds_loop {
    queue req;          /* client to server */
    queue rep;          /* server to client */
    teds_server *srv;
    end client_end;
    end server_end;
    teds_transport client;
    teds_transport server;
};

static int q_push(queue *q, const teds_msg *m) {
    if (q->count == QUEUE_LEN) {
        return -1;
    }
    q->msg[(q->head + q->count) % QUEUE_LEN] = *m;
    q->count++;
    return 0;
}

static int q_pop(queue *q, teds_msg *m) {
    if (q->count == 0) {
        return 0;
    }
    *m = q->msg[q->head];
    q->head = (q->head + 1) % QUEUE_LEN;
    q->count--;
    return 1;
}

/* Client end: requests go into req, replies come out of rep. */
static int c_send(void *ctx, const teds_msg *m) {
    end *e = ctx;
    return q_push(&e->loop->req, m);
}
static int c_recv(void *ctx, teds_msg *m) {
    end *e = ctx;
    return q_pop(&e->loop->rep, m);
}
static void c_kick(void *ctx) {
    end *e = ctx;
    teds_server_poll(e->loop->srv);
}

/* Server end: replies go into rep, requests come out of req. */
static int s_send(void *ctx, const teds_msg *m) {
    end *e = ctx;
    return q_push(&e->loop->rep, m);
}
static int s_recv(void *ctx, teds_msg *m) {
    end *e = ctx;
    return q_pop(&e->loop->req, m);
}

int teds_loop_create(teds_loop **out, teds_server *srv) {
    teds_loop *l = calloc(1, sizeof *l);
    if (l == NULL) {
        return TEDS_ERR_NOMEM;
    }
    l->srv = srv;
    l->client_end = (end){ l, 1 };
    l->server_end = (end){ l, 0 };
    l->client = (teds_transport){ c_send, c_recv, c_kick, &l->client_end };
    l->server = (teds_transport){ s_send, s_recv, NULL, &l->server_end };
    *out = l;
    return TEDS_OK;
}

void teds_loop_free(teds_loop *l) {
    free(l);
}

const teds_transport *teds_loop_client(teds_loop *l) {
    return &l->client;
}

const teds_transport *teds_loop_server(teds_loop *l) {
    return &l->server;
}

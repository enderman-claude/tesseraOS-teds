/* teds wire protocol. Every message is one fixed-size teds_msg. Layout is part of the
 * ABI: change it only together with TEDS_PROTOCOL_VERSION. */
#ifndef TEDS_PROTOCOL_H
#define TEDS_PROTOCOL_H

#include <stdint.h>

#define TEDS_PROTOCOL_VERSION 1u
#define TEDS_MSG_SIZE 56u

enum teds_op {
    TEDS_OP_HELLO = 1,
    TEDS_OP_HELLO_REPLY = 2,
    TEDS_OP_CREATE_SURFACE = 3,
    TEDS_OP_COMMIT = 4,
    TEDS_OP_DESTROY_SURFACE = 5,
    TEDS_OP_DIRECT_REQUEST = 6,
    TEDS_OP_DIRECT_RELEASE = 7,
    TEDS_OP_REPLY = 8,
    TEDS_OP_GOODBYE = 9,         /* client disconnect: teds releases everything it held */
};

/* Capability bits, sent in HELLO and granted in HELLO_REPLY. */
enum teds_cap {
    TEDS_CAP_CLIENT = 1u << 0,   /* display-client */
    TEDS_CAP_DIRECT = 1u << 1,   /* display-direct */
};

/* Status codes. Replies carry 0 on success and one of these on failure. */
enum teds_status {
    TEDS_OK = 0,
    TEDS_ERR_DENIED = -1,        /* missing capability */
    TEDS_ERR_BUSY = -2,          /* direct mode held by another client */
    TEDS_ERR_NOMEM = -3,         /* no free client or surface slot */
    TEDS_ERR_INVALID = -4,       /* bad size, bad id, or bad state */
    TEDS_ERR_UNSUPPORTED = -5,   /* backend cannot do this (e.g. no framebuffer) */
    TEDS_ERR_PROTOCOL = -6,      /* wrong op or serial in a reply */
};

typedef struct teds_msg {
    uint16_t op;
    uint16_t reserved;
    uint32_t serial;             /* request id, echoed in the reply */
    uint32_t surface;            /* surface id, where relevant */
    int32_t status;              /* replies only */
    union {
        struct { uint32_t version; uint32_t caps; } hello;
        struct { uint32_t client_id; uint32_t caps; } hello_reply;
        struct { int32_t x; int32_t y; uint32_t w; uint32_t h; uint64_t mem; } create;
        struct { uint32_t x; uint32_t y; uint32_t w; uint32_t h; } commit;
        struct { uint32_t w; uint32_t h; uint32_t pitch; uint32_t format; uint64_t mem; } direct;
        uint8_t raw[32];
    } u;
} teds_msg;

#endif

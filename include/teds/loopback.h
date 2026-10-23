/* In-process transport for host tests and tools. A loopback pair connects one client to one
 * server, runs the server from the client side (kick), and needs no IPC. */
#ifndef TEDS_LOOPBACK_H
#define TEDS_LOOPBACK_H

#include "teds/server.h"
#include "teds/teds.h"

typedef struct teds_loop teds_loop;

int teds_loop_create(teds_loop **out, teds_server *srv);
void teds_loop_free(teds_loop *l);
const teds_transport *teds_loop_client(teds_loop *l);   /* for teds_connect */
const teds_transport *teds_loop_server(teds_loop *l);   /* for teds_server_attach */

#endif

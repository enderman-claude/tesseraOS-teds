# teds: the Ender display server

teds is the display service for tesseraOS. It is an L7 user service in the tessera layer
model (see enderman-claude/tessera docs/spec/layers.md): it runs unprivileged and reaches
the framebuffer only through capabilities that the kernel grants.

Two ways for an app to use the display:

- **Client mode (default).** The app talks to teds over an IPC endpoint. It creates
  surfaces (its own pixel buffers, with a position and size), draws into them, and
  commits. teds composites every surface, in creation order, onto the screen. Apps
  never touch the hardware, and several apps can share the screen.
- **Direct mode.** A privileged app with the `display-direct` capability asks teds for
  exclusive use of the screen. teds suspends compositing, hands the app the framebuffer
  description and a mapping, and restores compositing when the app releases it or
  disconnects. Only one app can hold direct mode at a time.

## Protocol

Every message is one fixed-size `teds_msg` (56 bytes), so the IPC layer can carry it
without a length field. Requests carry a `serial`; the reply echoes it. Failures come back
as a reply with a negative `status`. The op codes and layout are in
`include/teds/protocol.h`.

| Op | Direction | Purpose |
|---|---|---|
| HELLO | client -> teds | Version and requested capabilities |
| HELLO_REPLY | teds -> client | Client id and granted capabilities |
| CREATE_SURFACE | client -> teds | Size, position, and the pixel buffer |
| COMMIT | client -> teds | Surface changed; damage rectangle |
| DESTROY_SURFACE | client -> teds | Remove a surface |
| DIRECT_REQUEST | client -> teds | Ask for exclusive framebuffer access |
| DIRECT_RELEASE | client -> teds | Give exclusive access back |
| REPLY | teds -> client | Generic ack, with status |

## Capabilities

teds checks two capabilities:

- `display-client`: may connect and use client mode. Granted to all L8 apps by default.
- `display-direct`: may request direct mode. Granted by the supervisor only to programs
  whose manifest asks for it.

In tessera this maps onto the endpoint capability (teds' IPC endpoint) and a new
`display` syscall group. Neither is defined in tessera yet, so the checks here are plain
flags on the connection.

## Memory

Surface buffers and the direct framebuffer are shared memory. The final design passes a
memory capability over IPC. Until tessera has memory objects, the transport passes the raw
pointer, and only the in-process loopback transport uses that path.

## Pixel format

`0x00RRGGBB` in 32 bits per pixel, row by row, the same format as ember-gfx. The
framebuffer reports its own format, and direct-mode clients must check it before drawing.

## Status

| Part | State |
|---|---|
| Protocol and client API | Implemented; host-tested over loopback |
| Server dispatch, surfaces, compositing | Implemented; host-tested |
| Direct mode grant and release | Implemented; host-tested |
| Disconnect cleanup (surfaces, direct mode) | Implemented; host-tested, ASan and UBSan clean |
| Capability checks | Flags from the attach-time allowed set; not yet tessera capabilities |
| Framebuffer | Host memory only; tessera GOP path is a stub (`src/tessera/`) |
| IPC transport | Loopback for host tests; tessera endpoint transport is a stub |
| Input | Stub (not implemented) |

## Client API notes

- `teds_connect` sends HELLO with the requested capability bits. The server grants the
  intersection with the allowed set it was attached with.
- `teds_disconnect` sends GOODBYE. The server releases direct mode and removes the client's
  surfaces. The client frees surfaces it still holds, so their handles are invalid after it.
- Calls are synchronous. The loopback transport runs the server from `kick`. A real transport
  leaves `kick` empty because teds is its own task.

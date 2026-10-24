# teds

The Ender display server for tesseraOS. Apps reach the screen in one of two ways:

- **Client mode:** apps create surfaces, draw into their own buffers and commit. teds
  composites them.
- **Direct mode:** a program with the `display-direct` capability gets exclusive access to
  the framebuffer, for OSD, fullscreen and other uses that need it.

See [docs/DESIGN.md](docs/DESIGN.md) for the protocol, capabilities and status.
`include/teds/` holds the protocol and the client and server APIs. `make test` builds the
library and runs the host tests over an in-process loopback transport. The tessera IPC
transport and framebuffer are stubs that return `TEDS_ERR_UNSUPPORTED`.

## Related repos

- [tesseraOS](https://github.com/enderman-claude/tesseraOS): the OS teds runs on.
- [tesseraOS-xteds](https://github.com/enderman-claude/tesseraOS-xteds): X11 compatibility. xteds is a teds client, the way XWayland is a Wayland client.
- [tessera](https://github.com/enderman-claude/tessera): the kernel whose IPC endpoints and capabilities teds uses.
- [ember-gfx](https://github.com/enderman-claude/ember-gfx): graphics library whose tesseraOS backend is a stub for teds.

*Footnote: teds never runs out of things to do, because it always has clients.*

## License

Not yet chosen.

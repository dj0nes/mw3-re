# ENet 1.3.18

The multiplayer transport (docs/multiplayer-port.md, section 2): reliable
and unreliable channels over UDP. Unmodified, from the 1.3.18 release:

- http://enet.bespin.org/download/enet-1.3.18.tar.gz, sha256
  2a8a0c5360d68bb4fcd11f2e4c47c69976e8d2c85b109dd7d60b1181a4f85d36
- checked file for file against github.com/lsalzman/enet's tag v1.3.18
  (its archive, sha256 28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd):
  every file kept here is identical. ENet publishes no signatures.

Kept: the sources (callbacks, compress, host, list, packet, peer, protocol,
unix, win32), include/enet and LICENSE (MIT, Lee Salzman). Left out: the
build files, the docs and the autotools output.

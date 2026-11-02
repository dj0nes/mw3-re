# Network

Mech3.exe 1.2, 2026-10-19. Not ported; this is enough to plan the LAN work.

## Layers

- **zNetwork** (`znet_session.cpp`, `znet_dplay.cpp`, about 270 functions from
  0x586c20 to 0x58fc70): a wrapper around DirectPlay 4 (`IDirectPlay4A`).
  Connection types TCP/IP, IPX and modem; lobby launch; error strings for every
  `DPERR_*`. Sends go through 0x5888a0 (`SendEx`, vtable +0xc4, asynchronous
  when the provider allows: "NOT Using Asynchronous Sends." otherwise).
  Receiving is 0x588930 (`Receive`, +0x64), the `poll_network` hook that
  `zGameApp`'s main loop calls each frame; 0x588a70 handles DirectPlay's own
  system messages (player joined, player data changed, ...).
- **MW3 messages**, built in the game code around 0x46f160-0x470380 (after
  `mech_reader.cpp`) and in `mission.cpp`: each is `uint16 type, uint16 size,
  payload`. Seen so far: 0x1f text (a chat line), 0x1a an event that copies a
  block of state, 0x18 built by an object's vtable +0x58, a score/team update
  (0x470170, `[0x60861c]` pilot data), and a 7-dword record with a position
  and a flag bit 0x80 (0x46fde0).

## Sync model

State/event sync, not lockstep (`network-messages.md` has every message type).
No message carries a frame number or input; the poll never waits. Each mech's
state (type 0xf) goes out unreliable on a timer, 2-10 times a second depending
on the player count and a lobby setting, with the full damage state every 26th
time; receivers extrapolate using a latency measured with pings (type 0x10).
Each peer owns its own mech's movement, firing, damage and death; the host owns
the clock and game over, scores, lobby settings and craters. Messages go to
every player directly, not through the host.

## For the rebuild

DirectPlay is gone (FIX (A)): the transport is ENet over UDP (`net_enet`,
multiplayer-port.md), with a LAN broadcast for finding games, carrying the
same MW3 message types. Playing against the original exe is not possible and
not a goal (Owen, 2026-10-31: faithful to the game, a modern transport in
DirectPlay's place, no emulation of it).

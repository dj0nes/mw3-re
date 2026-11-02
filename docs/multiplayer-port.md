# The multiplayer port: the plan

Mech3.exe 1.2, 2026-10-31. A plan only: nothing here is ported yet. It builds
on `network.md` (the layers) and `network-messages.md` (every message), and
covers the deferred list in `program.md` (0x00588930, 0x00586c20, the chat
panel 0x0041ab90, the Multiplayer State's screens 0x00493260, 0x00495f90,
0x0049bd80, the session flag 0x00586ce0).

**In short.** The game talks to DirectPlay 4 through one thin layer,
zNetwork (about 130 functions, 0x00586c20-0x0058b000), and to zNetwork through
about 50 entry points. Keep the game side and the screens as the exe has them,
and replace only what is under zNetwork's entry points with a transport of
our own: **ENet** (MIT) carrying the same MW3 messages, guaranteed ones on a
reliable channel and the rest unsequenced, peer to peer as DirectPlay was,
with a LAN broadcast to find games and a direct `IP:port`. Playing against
the original exe is not possible and not a goal. About 27 sittings: 15 game
side, 12 front end.

## 1. What the game uses of DirectPlay

The objects (found from the exe, not assumed):

| Global | Object | Made by |
|---|---|---|
| [0x00730724] | IDirectPlay4A | CoCreateInstance(CLSID_DirectPlay d1eb6d20-8923-11d0-9d97-00a0c90a43cb, IID_IDirectPlay4A 0ab1c531-4745-11d1-a7a1-0000f803abfc), 0x00589540 and 0x0058a5f0 |
| [0x00730728] | IDirectPlayLobby3A | DPLAYX ordinal 4 (DirectPlayLobbyCreateA, the only DPLAYX import, thunk 0x005ac650), 0x005899d3 / 0x00589f1e |
| [0x007307e0] | a game service's score report, CLSID 3faf0afe-b48b-11d2-8a51-00c04f8ef4e9 (IID ...afd; Microsoft's range, from late 1998: most likely the MSN Gaming Zone's) | 0x00589c20 from 0x00589d00, only with a lobby's launch ([0x00608e24]): the lobby's scores (0x00498550) give it each player's score and team and 2 a win, 8 a shared win, 4 a loss, 0x100 gone (0x00589df0), then send (0x00589e30). Not ported (FIX (A), piece 11a): the service is gone |

The IDirectPlay4A methods called (vtable slots seen in the code; `+off`):

| Slot | Method | Where (entry point) | What for |
|---|---|---|---|
| +0x60 | Open | 0x00586f60, 0x00586fb0 (via 0x005874d7/0x00587593) | host (create) or join a session |
| +0x38 | GetCaps | same, 0x00589b93 | the provider's caps: async sends allowed ([0x00730765]) |
| +0x34 | EnumSessions | 0x00586ff0 (0x005871dc) | the game list |
| +0x8c | EnumConnections | 0x005881f0 (0x005881ae) | the service providers (TCP/IP, IPX, modem, serial) for the connection list |
| +0x98 | InitializeConnection | 0x0058a06a, 0x0058a42c | bind the chosen provider and its address |
| +0x58 / +0x7c | Get/SetSessionDesc | 0x00586e10, 0x00586f20 (0x00587733) | the session's name, player limit and the lobby's settings words |
| +0x18 | CreatePlayer (likely: the slot at 0x005887b4, its object not traced to [0x00730724]) | the open path | the local player |
| +0x24 | DestroyPlayer | 0x00588660, 0x005886a0 | leave; the host kicks |
| +0x14 / +0x20 | CreateGroup / DestroyGroup | 0x00588390, 0x00588590 | teams |
| +0x0c / +0x1c | AddPlayerToGroup / DeletePlayerFromGroup | 0x005884c0, 0x00588500 | join or leave a team |
| +0x2c / +0x28 / +0x30 | EnumGroups / EnumGroupPlayers / EnumPlayers | 0x00588200, 0x00588300, 0x005885c0, 0x00588600 | the lobby's lists |
| +0x50 / +0x74 | Get/SetPlayerData | 0x00589770, 0x00589830 | each player's record (mech, skin, ready) replicated by DirectPlay |
| +0x3c / +0x6c | Get/SetGroupData | 0x005897f0, 0x005897b0 | each team's record |
| +0xc4 | SendEx | 0x005888a0 `znet_send(msg, size, guaranteed, to)` | every MW3 message; DPSEND_GUARANTEED (1), DPSEND_ASYNC (0x200) when allowed; `to` 0 = all players, or one player, or a team group (team chat) |
| +0x64 | Receive | 0x00588930 `znet_poll(max, free)`, once a frame from zGameApp (0x0058fdf3) | every message; DirectPlay's system ones (from 0) through 0x00588a70 into MW3 messages 2-0xe |
| +0x10 | Close | 0x00586c20 (the shutdown) | |

The lobby object: GetConnectionSettings (+0x20, three calls), SetConnectionSettings
(+0x30), ConnectEx (+0x3c), from the start-up's 0x005899b0 and 0x00589a50
(0x00480020): **the lobby-launch path**, a game started by a lobby program
(MPlayer, the Zone) with its session already chosen ("Failed to connect to
the lobby"). No such service exists any more.

zNetwork's own entry points the game calls (callers outside 0x00586c20-0x0058b000):

- Session state: 0x00586ce0 "a network game is up" (5 callers), 0x00586d10 the
  local player id (29), 0x00586d20 "I am the host" (17), 0x00586e00 / 0x00586de0
  / 0x00586ea0 / 0x00586ed0 / 0x00586f20 / 0x00586f30 the session description
  and its words, 0x00586f60 / 0x00586fb0 host or join, 0x00586ff0 the game
  list, 0x00586c20 the shutdown (3).
- Messages: 0x005888a0 send (11 callers), 0x00588930 poll (zGameApp),
  0x00587aa0 / 0x00587b20 / 0x00587a00 register, remove, clear a handler
  (the dispatcher 0x00587c40, network-messages.md Plumbing).
- Players and teams: 0x00588a60 a player's record (+0x44: its data; 7),
  0x005898b0 / 0x00589910 each player / each team through a callback, the
  group calls above, 0x00589600 / 0x00589680 the lists' lookups.
- Connections: 0x005881f0 the providers, 0x00589540 / 0x0058a110 the set-up
  and the async-send flag, 0x005899b0 / 0x00589a50 the lobby launch,
  0x00589c00 / 0x00589d00 / 0x00589df0 / 0x00589e30 the unidentified object.
- Errors: 0x0058a8c0 the DPERR_* names, 0x0058aec0 (from 0x004d5120).

Everything above 0x0058b000 that the earlier list showed (0x0058b060,
0x0058b160, 0x0058b390, 0x0058f2a0) is called from all over the game and is
not network code (a later module's helpers).

## 2. The replacement transport

**What it must give** (DirectPlay's semantics the game leans on):
guaranteed and not-guaranteed sends; guaranteed ones in order between two
players; to all, to one, or to a team; the system events (a player joined or
left, its data changed, a team made or joined, the host changed, an async
send done with its send time); replicated player and team data; a session
with a host, a name and a player limit; finding sessions; up to 8 players.

**Recommended: ENet** (enet.bespin.org, 1.3.x, the MIT licence; C, a few
thousand lines, vendored in `third_party/` as miniz and pl_mpeg are). It is UDP
with reliable ordered channels and unreliable ones, its own connection
handshake, timeouts and round-trip measure. Two channels: 0 reliable (every
guaranteed message), 1 unreliable unsequenced (the mech states 0xf, pings,
knockback). A full mesh as DirectPlay's peer to peer was: each player
connects to every other, "to all" is a send to each peer, messages never go
through the host. The host additionally answers discovery and owns the
session record and the player and team lists, which it pushes on change
(our system messages, reliable).

The alternative, **plain UDP with our own reliable channel**, needs the
acknowledgements, resends, ordering, fragmentation (the 0x14 table and a
lobby's records pass 512 bytes), keep-alives and connection handling that
ENet already has, tested; more code to own for nothing the game needs. TCP
for the guaranteed messages would stall the state packets behind a lost one.

**Finding games**: a LAN broadcast (UDP, one port, say 27931) every second
from the game list's screen; hosts answer with the session's name, players /
limit, the mission database, the game's version and the data version
(`detect_data_version`), and its ENet port. Plus **direct `IP:port`** (the
connection screen's CONNECT_INFO_EDIT, the field the exe used for TCP/IP's
address), and `--host` / `--join HOST[:PORT]` on the command line for tests
and dedicated starts. The connection list shows "LAN / Internet (IP)" in
place of DirectPlay's providers; IPX, modem and serial go (FIX (A)); the
modem buttons (CALL, ANSWER, DIAL) stay hidden. No NAT traversal:
internet play needs the host's UDP port forwarded, as most DirectPlay games
did; a relay or hole punching can come later if Owen wants it (the lead,
2026-10-31: ENet 1.3.x approved, IPX, modem, serial and the lobby launch
dropped).

**Host migration**: DirectPlay picked a new host (DPSYS_HOST, 0x00588ebb)
and the game follows it (type 0xd). Ours: the lowest remaining player id
takes over and says so with 0xd, as the exe does; its session record is the
last one pushed.

**The lobby launch**: not ported (no lobby services). `--join` covers
starting straight into a game.

**Against the original exe over DirectPlay**: not possible in practice, and
not a goal. It would need DirectPlay 4's own wire protocol (its name server,
its system messages, its guaranteed-delivery layer, all undocumented)
reimplemented, the exe running somewhere that still has DPLAYX (Windows 11
installs DirectPlay only as a legacy feature), and the two sides' game logic
in step; the rebuild's fixes (behind `bugs`) would also desynchronise it.
Two rebuilds play each other; a rebuild and Mech3.exe do not.

## 3. Payload hazards: 64-bit and byte order

The wire is little-endian x86 data laid out by offset; the port writes and
reads each field by offset with little-endian helpers (no struct casts onto
the buffer, no `#pragma pack` structs), so nothing depends on the host's
packing or pointer size. Big-endian targets are not planned, but the helpers
make them free.

- **Pointers in messages**: the local-only types 2 and 0xe carry a pointer
  at +4 to DirectPlay's system message (network-messages.md); 4 bytes in the
  exe, 8 here. They never go on the wire: in the port they carry an index or
  their own fields, not a pointer cast to 32 bits.
- **0x1a, an effect animation copied raw** (0x0046feb0, its size from
  0x0050ca20): the record holds the exe's pointers (its definition, its
  node) as well as values. The port must send what the record means (the
  animation's name or index, its target's player id or node name, its
  values) and rebuild it on the receiver; a raw copy of our record would put
  64-bit pointers on the wire and be meaningless on another machine anyway.
  The largest hazard of the set.
- **Player ids**: DirectPlay's DPIDs are 32-bit; ours are 32-bit too, 0
  never used (0 = all players, and the system sender); the mech's owner
  (+0x6fc) keeps a 32-bit id, not a pointer.
- **0xf, the mech state**: int16 velocities (x 1/127), signed bytes, a
  packed flag word (bits 0-31, network-messages.md), floats: all little-endian
  by offset; the fire events and damage records after it are counted in the
  flag word, so the receiver must check the size against the counts before
  reading (the exe trusts it).
- **0x14 scores**: int16 count then 12-byte entries; **0x1f chat**: text with
  its length; **0x1e** 8 ids; **0x20** a word and four int16: sizes checked
  on receipt.
- **Player and team data** (the records Set/GetPlayerData replicate): laid
  out by the lobby code; ported field by field like the messages.
- Floats are IEEE single on every target; the exe's x87 24-bit rounding does
  not reach the wire (values are stored as floats).

## 4. Tests

- **The message builders and handlers against the exe** (Unicorn,
  `tests/differential/test_net_messages.py` with a `probe_net.cpp`): run the
  exe's builders (0x0046f160 for each kind, 0x00470170, 0x00470320,
  0x00470380, 0x004700e0, 0x0046fde0's echo) on a stub mech with the send
  0x005888a0 hooked, and compare the bytes with the port's; run the handlers
  (0x0046e4d0 the state onto a replica, 0x0046eba0, 0x00470740, 0x004705b0,
  0x0046fff0, 0x00470a00 the latency) on fixed buffers and compare the
  replica. Breaks: a field's offset or scale changed.
- **Two processes on one machine**: `mw3 --host --hidden --frames N` and
  `mw3 --join 127.0.0.1 --hidden --frames N`, scripted through the lobby
  (both ready, the host launches c1m1); each logs what it sees (players,
  the other's mech place each second, kills, chat). Checks: both list both
  players, each sees the other's mech within a metre of where the other
  logs it (dead reckoning on), a scripted kill reaches the scoreboard on both,
  a chat line arrives, the host's leaving hands the host over.
- **A loss and lag shim** in our transport (drop %, delay, reorder, from an
  environment variable), for the two-process test at 5% loss and 100 ms.
- **A soak test**: four processes, 30 minutes, the shim on: no crash, memory
  flat, every guaranteed message delivered (counters), states converge after
  the shim is turned off.
- **The receive path fuzzed**: random and truncated messages through the
  dispatcher must never crash (the size checks above).
- In-process: a loopback transport (two zNetwork instances in one test
  process) for the unit tests of sessions, players, teams and messages.

## 5. The porting order

Sitting-sized pieces, each with its tests and break checks. Sizes are
sittings (one assignment's work). The decompiled sizes: zNetwork 129
functions (about 4,050 lines of pseudocode), the game's network code at
0x0046ceb0-0x00470c00 55 functions (2,970), the Multiplayer State's screens
149 (7,390), the chat panel 6 (350).

**Game side (about 15)**

1. zNetwork's interface as the game sees it (the entry points of section 1
   as one class with a platform transport under it), the session flag
   0x00586ce0, the local id and host checks, and an in-process loopback
   transport. Unit tests. (2)
2. The dispatcher: handler registration (0x00587aa0, 0x00587b20,
   0x00587a00), dispatch (0x00587c40), the poll (0x00588930) and the system
   messages into MW3 messages 2-0xe (0x00588a70). Differential test of the
   dispatcher's order and the translation. (1)
3. Players, teams and their data: the 0xec-byte player records, groups,
   Set/GetPlayerData and GroupData replication, the host id and host
   migration (0xd, 0x00586d50). Loopback tests. (2)
4. **The transport** (after the lead's OK on the choice): ENet vendored,
   the two channels, the mesh, connect and leave, the loss shim, LAN
   discovery and direct IP:port, `--host` / `--join`. Two-process smoke
   test of sessions and players. (3)
5. The mission's network start: the handler registrations at 0x0046ceb0,
   the in-game dispatcher 0x0046d880, the remote mechs' list and their
   creation from a first 0xf (0x0046d450), leaving (6, 0x0046d3b0). (2)
6. The mech state 0xf: the builder (0x0046f160 kind 0xf, 0x0046ef70's timer
   and rate table 0x00470b40, the full damage resend), the apply with dead
   reckoning (0x0046e4d0), fire events replayed (0x004b08c0), damage records;
   the pings 0x10 and the latency (0x00470a00, 0x00470ab0) and the send-done
   time (0xe). Differential tests. The two-process test sees mechs move. (3)
7. The events: 0x12 respawn, 0x13 kill and 0x14 scores (0x00470170),
   0x17 posture, 0x18 knockback, 0x21 a part shot off, 0x19 craters through
   the host, 0x1a effects (by meaning, section 3), 0x1b drop points, 0x11
   the clock and game over. Differential tests per message. (2)
8. The rules check (the CRC of the rules block, 0x00576c10, about every 51 s)
   and the game's end and return to the lobby. The soak test. (1)

**Front end (about 12)**

9. The Multiplayer State itself (0x0049c900, its handler 0x0049cbb0, the
   pilot made on entry) and the connection screen 0x00493260 with
   CONNECTION_LIST as LAN / IP, CONNECT_INFO_EDIT the address, FIND_GAME;
   the modem buttons hidden. (2)
10. The game list and host screen (EnumSessions' list, join, create a game
    with its name and limit): its controls from dialog.zrd's MULTIPLAYER
    dialogs. (2)
    Owen's decisions for 9-10 (2026-10-31, through the lead):
    - The original's screens stay. The connection screen keeps its layout
      and offers games on the LAN (found automatically, click to join),
      Join by address (host:port) and Host a game; no IPX, modem or serial.
    - Internet play at "level 1", with no server of ours: UPnP port mapping
      when hosting (miniupnpc, BSD, vendored as ENet is, its licence in
      THIRD-PARTY), removed on close and falling back quietly when the
      router says no; the host's screen shows its address (the public one
      from UPnP's external IP when known, and the LAN one) with a copy
      button; a clear line when the port could not be opened ("forward UDP
      27930 on your router").
    - Room on the connection screen for a later "Browse internet games" (a
      master server or punch-through, after v1.0).
    - Tests: UPnP against a fake IGD responder on loopback.
11. The lobby screen 0x00495f90: players and teams (0x1e slots, 0x00588200
    lists), the host's settings (0x20: the check boxes such as
    AMMO_CHECKBOX, the four lists, DATABASE_LIST's mission), ready and
    launch, the score table between games (0x14 in the lobby, 0x0049b150),
    being kicked (7, 0x0049a1d0). (4)
12. The lobby chat (CHAT_REC_LIST, CHAT_ENTER_EDIT, CHAT_TEAM_CHECKBOX:
    0x1f to all or to the team, 0x004998b0 / 0x00499610). (1)
13. The New Team dialog 0x0049bd80 (MULTIPLAYER_NEW_TEAM_DIALOG: its name,
    join, SKIN_LIST). (1)
14. The in-game chat panel 0x0041ab90 (0x00470380 / 0x00470490 on the HUD)
    and the multiplayer briefing (mpinfo.zrd) and debriefing's scores. (2)

Then the two-process and soak tests over the whole flow, menus included.

## Piece 9's plan (2026-11-01)

The connection screen 0x00493260 (MULTIPLAYER_DIALOG, its controls in
dialog.zrd) asks zNetwork for the service providers (0x005881f0 into
CONNECTION_LIST), makes a connection for the one chosen (0x0058a110: kind
0 modem, 1 IPX, 2 TCP/IP with an address, 3 serial; CONNECT_CHECKBOX
connects it, 0x004949f0), and searches for games through it
(0x00586ff0, EnumSessions with ALL | RETURNSTATUS, asynchronous: the
screen's update asks again every 0.5 s, DPERR_CONNECTING (0x8877015e)
while it waits; SESSION_LIST from the answers, 0x00494510). FIND GAME
(0x004957c0) gives the TCP/IP connection CONNECT_INFO_EDIT's address
(empty: DirectPlay's broadcast on the LAN), JOIN (0x00495490) joins the
chosen session (the game's version word checked: 0xfdd / 0xfde), HOST
(0x00495600) opens one named NEW_SESSION_NAME with MAX_PLAYERS_LIST's
limit and TEAM_CHECKBOX's team play, both go on to the lobby
(0x0049cb00(2, 1)); CANCEL (0x004953a0 case 1) the network off and back.

Ours, in three pieces:

- **9a, zNetwork's connections and search over the transport.** One
  provider by design (the lead, 2026-11-01: IPX, modem and serial do not
  exist on modern systems, so they are not listed, not even greyed out),
  TCP/IP's kind 2, shown as "LAN / Internet (UDP)": its address
  empty searches the LAN (our discovery broadcast, port 27931), else asks
  that host directly; the provider list, the connection object and its
  calls (open, the address) kept as the exe's shape. 0x00586ff0 asks the
  transport (`Transport::find_sessions`: not blocking; "searching" as
  DPERR_CONNECTING, the answers as session descriptions: name, players,
  limit, the user words with the game's version, the address to join).
  The discovery answer gains the user words and the version (sizes and
  NUL-ended names checked: a peer's answer is untrusted, piece 8c's rule).
  Tests: the search against an answering host and a silent address, and
  a broken answer.
- **9b, the screen.** The constructor, its controls (the modem's ANSWER,
  DIAL and CALL hidden), the connect, the update's polling, the session
  list, JOIN, HOST, FIND GAME, CANCEL, the info lines; the state's page
  switch 0x0049cb20, the network on and off 0x0047fc40, the pilot handed
  on 0x00494920. Owen's: the LAN searched as soon as the screen is up
  (no FIND needed), an address in CONNECT_INFO_EDIT and FIND GAME or
  JOIN to reach one host. Tests: the screen's logic through its hooks,
  and two processes, one hosting from the screen and the other finding
  it on the LAN and joining, both reaching the lobby page.
- **9c, internet hosting.** UPnP (miniupnpc, BSD, vendored as ENet is,
  its licence in THIRD-PARTY) maps UDP 27930 when hosting and removes it
  on close; no router or a refusal falls back quietly to LAN hosting with
  the line "Could not open the port: forward UDP 27930 on your router".
  The host's addresses (the public one from the router when known, the
  LAN one) shown with a copy button (SDL's clipboard); on the lobby too
  when piece 11 ports it (the host waits there for friends). A router
  that gives a private or carrier-grade NAT address as its public one
  (10.x, 100.64-127.x, 172.16-31.x, 192.168.x) cannot be reached from the
  internet: "Your internet provider blocks hosting: use a LAN, or a VPN
  such as Tailscale", and that address is not offered to share. Owen's
  "level 1, between friends": plain addresses, the copy button and the
  forward line, no server and no codes. Tests against a
  fake IGD on loopback (its description, AddPortMapping,
  GetExternalIPAddress, DeletePortMapping), and with none answering.

## After v1.0

- Matchmaking (Owen, 2026-10-31, through the lead): someday, after Pirate's
  Moon and everything else, and only if many people play: a small master
  server with a game browser, NAT punch-through, and a relay when
  punch-through fails. The connection screen keeps room for it (a "Browse
  internet games" entry, pieces 9-10). No work on it now.

## Progress

- **Piece 1** (2026-10-31, mac4/mp): `gamez/znetwork/znet_session` holds
  zNetwork's globals (`znet::Session`: [0x007306c8] up, [0x00730764] a session,
  [0x0073072c] a lobby's, [0x00730765] async sends, [0x00730760] the host,
  [0x0073075c] the local player) over a `znet::Transport`, with 0x00586ce0
  `active`, 0x00586d10 `local_id`, 0x00586d20 `is_host`, 0x005888a0 `send`
  (from the local player; guaranteed; async with its send's id) and
  0x00586c20 `shutdown`, plus ours: `initialise`, `host_session`,
  `join_session`. `znet::Hub` is the in-process transport for the tests
  (every endpoint in one session, the first to host the host, ids from 1;
  system events as from 0: the kind, then the player). The program's
  `network_lobby_active`, `lobby_session`, `network_shutdown` and the damage
  code's `force_variant0` (a destroyed part's effect, 0x00586ce0) are wired;
  without a session they answer as the exe without one. Tests:
  `test_znet_session.py` (the three checks over 32 states and the send over
  24 against the exe, SendEx's arguments included; two breaks caught),
  `znet_session_*` in mw3tests (the hub; a break caught).

- **Piece 2** (2026-10-31): the handlers and the poll in `znet_session`.
  0x00587aa0 `on` (added at the end), 0x00587b20 `off` (every entry of that
  type and handler, std::remove and erase), 0x00587a00 `clear_handlers`,
  0x00587c40 `dispatch` (each handler of the message's type in order, with
  the sender and the message; ours, its size too), 0x00588930 `poll` (until
  the queue is empty or `max` handled, -1 all; under 4 bytes dropped and not
  counted; the system's to `Session::system_message`, the translation that
  comes with piece 3; the rest dispatched). DirectPlay's growing receive
  buffer is the transport's. Ours: a handler removed during a dispatch is
  marked and erased after it (the exe read the deleted node); one added
  meanwhile is reached, as the exe's walk reaches the list's new end. The
  shutdown now polls and clears the handlers first, as 0x00586c20 does.
  zGameApp's per-frame poll (`AppHooks::network_poll`, 0x0058fdf3) calls it.
  Tests: `test_znet_dispatch.py` (300 random handler sequences, 1,584
  calls, and 200 polls through a stand-in Receive with the buffer's growth,
  identical; two breaks caught), `znet_dispatch_while_handlers_change` and
  `znet_poll_over_the_hub` (a break caught). The system translation and the
  player and team lists moved into piece 3 (they are one function, 0x00588a70).

- **Piece 3a** (2026-10-31): `gamez/znetwork/znet_dplay` (the exe's
  znet_dplay.cpp): the player records (0xec bytes: byte 0 DPCAPS_ISHOST from
  GetPlayerCaps 0x00589870, the id, the names at +0x44 and +0x94, the team at
  +0xe4, -1 at +0xe8) and team records (0x54: the name, the id at +0x50) in
  their lists ([0x007307ac], [0x007307bc]), 0x00589700 `player`, 0x00588a60
  `player_name`, 0x005898b0 / 0x00589910 the walks, 0x00586d50 0xd's handler
  (the sender is the host; registered at the set-up), 0x0058ae10
  `network_error` (local type 0xc), and the translation 0x00588a70:
  create (a player's record and type 3, then the host's 0xd, then 4; a team's
  record and 4), destroy (6, or 7 for ourselves, then the record; a team's
  players' team cleared, its record, 8), into a team (the record's team, 5),
  out of one (2), data (9 a player, 0xa a team), name (0xb), host (the local
  record's byte, 0xd to all and to ourselves), the session description,
  send done (0xe), session lost (MSG_DPERR_SESSIONLOST as 0xc). System events
  come from the transport as `SystemEvent` fields (section 3), not DPMSG
  structs. Local type 2 carries the player (8 bytes as the exe's); 0xe the
  send's id and time (12 bytes, the exe's 8 a pointer). The hub gives names,
  the host's caps and a joiner the players already there. The program's
  `lobby_player_name` is `player_name` ("" for none). Tests:
  `test_znet_players.py` (250 random event sequences through the exe's own
  translation, lists and dispatcher: every local message, send and list
  after identical; two breaks caught), `znet_players_as_the_host`,
  `znet_players_as_a_joiner` (a break caught).
- Piece 3 was split: **3b** is the teams' and the data's calls (create,
  join, leave and destroy a team 0x00588390 / 0x005884c0 / 0x00588500 /
  0x00588590, the enumerations 0x00588200 / 0x00588300 / 0x005885c0 /
  0x00588600, kicks 0x00588660 / 0x005886a0, the players' and teams' data
  0x00589750 / 0x00589770 / 0x00589830 / 0x005897b0 / 0x005897f0) over the
  transport.

- **Piece 3b** (2026-10-31): the teams' and data's calls in `znet_dplay`:
  0x00588390 `create_team` (DPNAME's short and long names the name; the
  record kept; a failure reported, no record), 0x005884c0 `add_to_team`,
  0x00588500 `remove_from_team` (the record's team cleared), 0x00588590
  `destroy_team`, 0x00588200 `enum_teams` (the list made again; its callback
  0x00589300 takes the long name, DPNAME +0xc), 0x00588300 `enum_team_players`
  (0x005892c0: each member's record given the team), 0x005885c0
  `enum_players` (0x005893f0: records for the new), 0x00588660 `leave`,
  0x005886a0 `kick`, 0x00589770 / 0x00589750 / 0x00589830 a player's data
  (got, its size, set; DPGET_REMOTE, DPSET_GUARANTEED) and 0x005897f0 /
  0x005897b0 a team's. An add, removal or destroy changes the lists through
  its system event, as in the exe; a failed call is MSG_DPERR_GENERIC as
  local 0xc (the transport answers yes or no, not DirectPlay's code). The
  transport gains groups, kicks, enumerations and replicated data; the hub
  does them (the events to everyone, the setter not told of its own data, a
  kicked player told last). 0x00588600 (the players of a listed session) is
  the game list's (piece 10); 0x005886c0, the session's opening (its lists,
  our own record), piece 4's. Tests: `test_znet_teams.py` (250 call
  sequences through the exe's calls and its enumerations' own callbacks:
  every DirectPlay call and argument, answer, error message and list
  identical; two breaks caught), `znet_teams_data_and_a_kick` (a break
  caught).

- **Piece 4** (2026-10-31): the transport. ENet 1.3.18 vendored in
  `third_party/enet` (MIT, unmodified; its release tarball's sha256 noted, and
  every kept file identical to GitHub's v1.3.18 tag; ENet signs nothing), its
  notice in both THIRD-PARTY files. `frontend/sdl/net_enet`: `EnetTransport`
  (one ENet host per machine, listening and connecting; channel 0 reliable
  for guaranteed messages and ours, 1 unreliable; frames written by offset,
  little-endian). A joiner connects to the host (HELLO; WELCOME brings the
  session's description, its players with the addresses the host sees, the
  teams and the data), asks for its player (CREATE; the host gives the id
  and tells the others, PLAYER), then links to each other player (MESH); a
  link that comes before the host's word of its player is held until it.
  Teams and data go from whoever changes them to everyone (team ids
  0x10000 x the maker's id + n: no host round trip); team chat to the team's
  members; a kick (KICK) makes the player leave; the host's link lost, the
  lowest id left is the host (DPSYS_HOST). The send done (0xe) is at once:
  ENet queues without waiting. The loss and lag shim (`NetOptions` loss and
  delay, MW3_NET_LOSS / MW3_NET_DELAY): unreliable messages dropped, all
  held. LAN discovery: `DiscoveryAnswer` on UDP 27931 answers "MW3R?" with
  the session (name, players, limit, game port); `browse` asks the LAN's
  broadcast or given addresses. The game port 27930 (a host's default; a
  joiner's the system's). DirectPlay's Open and CreatePlayer apart in the
  transport, and the local player made as 0x005886c0 does
  (`znet::create_player`: enumerated first, CreatePlayer, our record at the
  list's end, the host's caps make it host and send 0xd); the joiner learns
  of those already there by the enumeration, not by events. `mw3 --host` /
  `--join HOST[:PORT]` (with `--net-name`, `--net-port`, `--net-bind`,
  `--net-say TEXT@N`) start a session before the game and log its events
  (ours, until the lobby screens). Tests: `test_znet_create.py` (0x005886c0
  over 40 cases identical; a break caught), `net_enet_three_machines_join_talk_and_part`
  and `net_enet_lan_discovery_and_the_shim` (mw3sdltests: three machines on
  127.0.0.1 with system-chosen ports, the mesh, teams, data, team chat, a
  kick, the host's leaving, discovery, the shim; a break caught),
  `sdl_two_processes_host_and_join` (two mw3 processes: the join, the chat
  line, the host passing on; a break caught). mingw compiles the transport
  and ENet for Windows. Not yet run on Windows itself, nor across two
  machines.

- Piece 5 was split: **5a** the start, the end, the router and a player
  leaving; **5b** the network mech (0x0046d450 with the player data read
  0x0046d820, the constructor 0x0046e280 and its controls).
- **Piece 5a** (2026-10-31): `mech3/mech_net` (original file unknown,
  between mech_reader.cpp and mech_salvage.cpp). The start 0x0046ceb0
  (once: the missing players' list emptied; 0xf-0x1b, 0x1f, 0x21 and 0x14 to
  the router 0x0046d880, 6 to 0x0046d3b0, 1 to 0x004d72a0 (returns 0), 0xe
  to 0x004709d0; zDecal's crater veto 0x0046fd30; the flag [0x00605fb4],
  the weapons' copy too; the anim log's network hook 0x0046feb0 with tag
  0xc, `zeff::log_set_net` = 0x0050d1d0; the network game's +0x7c, +0x80,
  +0x84 cleared; the dead reckoning's gains [0x005e55f0] = -6.5 and
  [0x005e55ec] = -0.3, the data's -3.5 before). The end 0x0046d100 undoes
  it: the handlers off, the veto and flag cleared, the pilot's mech's
  0x0041e580, each remote mech forgotten by the sensors, taken off the
  list and deleted, the local player's data set empty, the anim hook off,
  the force feedback stopped, the configs freed, the missing players' list emptied.
  The router: 0x19 straight to 0x0046fde0; the rest only from a player
  with a mech on the list [0x00605f84] (looked up by +0x6fc), each type to
  its handler (the mech as ECX for most; 0x00470740, 0x0046eba0 the
  message only; 0x0046ffd0, 0x004705b0 the mech on the stack too;
  0x00470490 the sender); 0x15 is registered but dropped; a first 0xf from
  a player without one calls 0x0046d450 and, once the mech is there, puts
  it at the state's position (vt+0x18), stamps timeGetTime at +0x73c,
  routes the message again and dead-reckons it a frame (vt+0x158). A
  player leaving (6): the HUD's 0x26a with their name for 4 s, the sensors'
  forget, the mech's delete (its destructor 0x0046e3c0 takes it off the
  remote and the dead mechs' lists), and the lobby slot's +0x18 set (the
  game's list at +0x48, 0x00495b10). The handlers of pieces 5b-7 and the
  lobby are `NetCalls` hooks until then; the crater veto and the anim hook
  are unset until piece 7 (craters stay local meanwhile). Wired: the
  briefing's launch and the quit box (`multiplayer_launch_prep`,
  `network_leave`). Tests: `test_mech_net.py` (400 scripts of starts, ends,
  messages and players leaving against the exe, the list carried along;
  two breaks caught) and `net_mission_over_the_hub` (mw3tests).

- Piece 5b was split again: **5b** the records (the local player's at the
  launch, another's read and unpacked), **5c** the network mech made from
  them (0x0046d450, the constructor 0x0046e280, its controls).
- **Piece 5b** (2026-10-31): the launch 0x0046da80 (`net_launch_player`,
  LaunchHooks::multiplayer): the state sends' globals [0x00605f9c..a4] and
  the mech's acknowledgement (+0x668 = 1, +0x66c = 0) cleared; the record
  (0x0046dd90's buffer: 2, the slot's skin, the mech's roster, then the
  packed loadout of the game's slot 0, Config::packed) set as the local
  player's data; the team masks from the lobby's team (0x00435910); the
  game's unlimited ammo (+0x8d, 0x004b0220) and outside view (+0x8e,
  0x0040ae70(1)); the scores (0x004361e0, a hook); then aiv.zrd's
  "remote_<slot>" (the lobby slot's +0x20), moved 10 m by the slot
  (0x0046e0a0: slots 2-4 +x, 1 and 5 none, the rest -x; 0-2 +z, 3 and 7
  none, the rest -z), the mech placed there (vt+0xdc) and the force
  feedback's spring started (0x004338a0). The mech's definition is loaded
  first and unused (0x004846a0). The record's read 0x0046d820
  (`net_read_record`, under 8 bytes none). `Config::unpacked` = 0x004259c0
  (named "remote config", made by the player; the engine clamped to the
  table; the armour tons come back doubled, then an armour item sets them
  again; FIX (bug): no items read past the record), with the ammo and
  weapon adders 0x00472210 (onto the like item unless over a ton) and
  0x00472330. The lobby's team and slot are hooks until piece 11. Tests:
  `test_config_unpack.py` (744 packed loadouts, every dfn's built-ins
  changed, identical with their weight and Validate; a break caught; a
  stock loadout unpacked is valid exactly when it was),
  `test_mech_net_launch.py` (600 launches identical; a break caught).

- **Piece 5c** (2026-10-31): the network mech. 0x0046d450
  (`net_mech_create`, the router's default for a first 0xf): the sender's
  record read (0x0046d820); a record of kind 2 gives the roster (+8), the
  skin (+4) and the packed loadout (+0xc); the definition (0x004846a0), the
  team (0x00435910), the loadout unpacked and checked (Validate without the
  checksum): a valid one is named "<player>'s Config" and kept on the list
  [0x00605f74], a failed one is freed and the player's id put on the
  missing list [0x00605f94] (a list of ids, written and never read). With a
  definition, a loadout and a roster: the controls (`NetControls`: the
  base's smoothing, owned by the mech, vt+0x18 0x004d7210), the type's data
  (0x00430d10), the mech (0x0046e280, `net_mech_new`: the mech's own build
  0x004149f0, the label object at +0x76c (0x005a00c0, GDI text: a hook),
  vtable 0x005c3508 (`Mech::remote`: its vt+0x5c answers yes), the owner
  +0x6fc, not colliding (+0x5b0), on the list, in the world, its torso in
  mode 2 (0x00466190 gives 2 for any network mech whatever it is asked),
  powered up, +0x740 the quaternion (1, 0, 0, 0)); its team masks, its
  skin (0x00414390, none past the definition's), its sensor range set again
  (0x004d6d20), +0x6f8 = 0; the host replays the anims and craters to it
  (0x004700a0, 0x00500c60 with 0x004700e0: a hook until piece 7). Its
  delete (the app's): off the lists, the label, the mech's own release,
  off the program's registry. FIX (bug): a record too short for a
  loadout's head gives none, one under 12 bytes reads zeros past its end,
  and the controls are deleted when the type's data fails. Tests:
  `test_mech_net_create.py` (800 creations identical; a break caught),
  `net_mech_made_from_a_record_c1m1_headless` (mw3tests: in c1m1 a second
  player over the hub joins with the pilot's mech, its mech is built, put
  at its first state's place, not colliding, in the world, torso mode 2;
  its leaving deletes it; a break caught).
  Open: the network controls' pilot tick is _purecall in the exe, and the
  mech's death calls it through vt+0xa8 (0x004aed50); whether a remote
  mech ever dies that way is piece 7's (the port's tick does nothing).

- Piece 6 split: **6a** the sending side, **6b** the receiving side (the
  state applied with dead reckoning 0x0046e4d0 / 0x0046ed70, the fire
  events 0x004b08c0, the pings' answer 0x00470a00, the send done 0x004709d0).
- **Piece 6a** (2026-10-31): the mech's vt+0x168 0x0046f160
  (`net_send_event`): the state 0xf (`net_state_message`, its layout in
  network-messages.md; the send's id back, the shots' records and damage
  bits cleared after), the ping 0x10 to the next player round the list
  (0x00470ab0, `net_next_player`; the host's every tenth ping checks the
  rules, piece 8's), a part's damage bit 0x15, a shot record 0x16 (the
  Weapon's slot, the manager's trigger state, the locked target's player);
  0x11-0x13, 0x17, 0x21 piece 7's (`NetCalls::send_other`). The local
  mechs' vt+0x158 0x0046ef70 (`net_mech_tick`): the state at the rate of
  0x00470b40 (`net_send_period`, [0x00608e28] the lobby's setting, the
  lobby's player count) with every part's damage each 26th, a ping every
  5.1 s, the one-minute warning (0xf0b) and the last second, the game's
  end state at 3 s left, the host's clock (0x11) every 5 s and each frame
  while ending, the game over once ended. Mech fields named: +0x678
  `net_shots`, +0x680 `net_damaged`; the network mech's ping fields
  (+0x6d8..+0x6f8, +0x6f8 the latency); MultiplayerGame +0x7c / +0x80 /
  +0x84 `clock_next` / `end_time` / `end_state` (floats and a state).
  `zmth::quat_log` = 0x005259c0. Wired: DamageCalls/WeaponCalls
  `net_event`, the update's and the dead list's `net_update` (a network
  mech's goes to piece 6b's reckoning). FIX (A): a shot's or state's
  target that is not a network mech gives 0 (the exe reads +0x6fc past a
  plain mech). Tests: `test_mech_net_send.py` (3000 sends and timer runs
  identical, the state's computed floats within float rounding; a break
  caught), and in `net_mech_made_from_a_record_c1m1_headless` the pilot's
  real state reaching the other player with its place. Not yet: the
  lobby's player count (0 until piece 11: the rate is then 0.5 s).

- **Piece 6b** (2026-10-31): the receive side. 0x0046e4d0
  (`net_state_apply`, the router's default for 0xf): the fixed part kept
  (+0x700, `NetStateIn`); unless the state says inactive, the velocity
  (x 1/127, +0x760), the throttle and turn through IControls (vt+0x20,
  +0x28), the torso's turn (0x00525280 `zmth::quat_two_angles`, 0x004646f0),
  the aim (quat_exp of its turn, carried on from the last by (latency +
  gap) / gap, a latency in seconds over a gap in milliseconds (FIX (bug),
  behind bugs: the latency in milliseconds); rotated onto
  (0, 0, -range) from the torso node: the weapons' aim, 0x004b0bc0), the
  heading quaternion (+0x750), the passive radar (vt+0xb8's light) and the
  target (the pilot, a network mech by player 0x00470c20, or none:
  0x004a7b40); then the shots replayed (0x004b08c0, the target a player id)
  and the damaged parts set (vt+0x3c, vt+0x40 x 1/255; the parts read only
  when the message holds more than the shots); the power, AMS, MASC, NARC,
  crouch, jump and the motion state's changes (from 0: the limp's end;
  from 1-9 to 0 its start; 12 / 13 the actions 6 / 5; from 10 the respawn,
  piece 7's). 0x0046ed70 (`net_reckon`, a network mech's vt+0x158): to the
  state's place and heading at once when its flag bit 0 says so, else
  blended (fast_exp at -6.5) with the place carried on at the velocity,
  damped at -0.3, and the heading slerped; set (vt+0x18), the matrix, the
  entity moved. 0x00470a00 (`net_ping_in`): the latency, (round trip -
  their delay - (our acked time - theirs)) x 0.0005. 0x004709d0
  (`net_send_done`): the pilot's state acknowledged. A network mech's own
  vt+0x168 is 0x0046ef50 (0): the sender answers nothing for it. Wired:
  the target's pick, the radar's light, the shots' target players
  (WeaponCalls::net_target). FIX (bug): a state under 0x3c bytes is
  dropped, and the payload is read only as far as the message goes. Tests:
  `test_mech_net_apply.py` (3000 applies, reckonings, pings and sends done
  identical, floats within rounding; a break caught), and the c1m1 test's
  second state followed (a break caught).

- Piece 7 split: **7a** a mech's events (respawn 0x12, kill 0x13 and the
  scores 0x14, posture 0x17, the collisions' push 0x18, a part shot off
  0x21), **7b** the world's (craters 0x19, effects 0x1a, drop points 0x1b,
  the clock and the game's end 0x11).
- **Piece 7a** (2026-10-31): the sends in `net_send_event`: 0x12 the place,
  guaranteed; 0x13 the victim (vt+0x160, the local id) and killer (a
  network mech's player, else the local id), guaranteed, then heard here
  too; 0x17 the action; 0x21 a network mech's part shot off here (the
  local id, its player, the part's id; only sent for a network mech's part:
  the exe reads +0x6fc unchecked); 0x18 from a collision with a network
  mech (CollideCalls::net_shove, 0x00470320: the damage, the push, to its
  player). The receivers: 0x0046ed10 the action queued; 0x0046ed30 a dead
  mech made again: 0x0046e0f0 (`net_mech_rebuild`) runs its destructor and
  constructor on its own memory (the same pointer, put back at the list's
  end) keeping its controls and loadout, then its sensors, skin by the old
  name (`SkinSet::named` = 0x00475b00), team, the snap bit off; then placed
  (0x00418e90); 0x0046eba0 the kill's HUD line (5 s, 0xf0a for a suicide,
  0xf09 with both names: a network mech's player, the pilot's name,
  "Unknown") and the host's score 0x00470170 (a suicide; a kill between
  teams, the score limit ending the game, state 2, the clock sent at once;
  then the scores 0x14 to all); 0x004708e0 the scores in; 0x004705b0 the
  push on the pilot (its torso's damage vt+0x38, the velocity); 0x0046fff0
  the pilot's part's structure to 0 (vt+0x3c). The lobby's scores, the
  HUD's refresh and a mech's name are hooks (the lobby is piece 11).
  Found while reading: a network mech's vt+0x164 (the damage handler) is
  0x0046ef50, returning 0, so it takes no damage here (its damage comes in
  its states); `Mech::take_damage` now returns for one. So Mech::die (and
  the controls' _purecall through vt+0xa8) is not reached through a hit;
  whether anything else kills a network mech stays for 7b's read. FIX (bug)
  behind bugs: a push makes the pusher the pilot's attacker (the exe sets
  the sender's own +0x664). Tests: `test_mech_net_events.py` (4000 events
  identical, and the push's fixed path; a break caught), the sender test's
  new kinds (3000 sends identical), and in c1m1 a dead remote mech made
  again in place by a 0x12 (a break caught).

- **Piece 7b** (2026-10-31): the world's events. Craters: zDecal's veto
  in a network game is 0x0046fd30 (`net_crater_veto`, set by the start): a
  crater of the pilot's own shot goes to the host as 0x19 (the ground's
  material by index, the place, the radius; the host handles its own at
  once) and is not made here; any other is not made at all; one with a
  negative radius (from a 0x19) passes, its radius made positive (the
  veto writes the caller's order). 0x0046fde0 (`net_crater_in`): the order
  from the defaults (0x004ff920) with the message's ground, place and
  negated radius; the host makes any and, when made, sends it on to all
  with bit 7; the others make only bit-7 ones. The host's replay to a
  joiner (0x004700a0, then 0x00500c60 with 0x004700e0, `net_host_replay`):
  the anim log's records it sends (0x0050d200 bit 1) and every crater of
  its history, with bit 7. Effects: the anim log's network hook 0x0046feb0
  (`net_anim_sent`): the record raw (its size by type, 0x0050ca20), to all,
  but not the mobile field base stations' (a name with "mfb_station_01")
  nor while one heard is replayed ([0x00605fb8]); 0x0046ff50
  (`net_effect_in`): replayed (0x0050ca90) unless one like it is logged
  (0x0050b680: the same node and name). New in zeff_anim_save:
  `log_record_size`, `log_flags`, `log_has`. Drop points: 0x0046ff90
  (`net_send_drop`, a repair station's id) and 0x0046ffd0 (the station's
  repair on the sender's mech, 0x0045a140); the stations' call to send it
  is the field bases' unported multiplayer side. The clock: the sender's
  0x11 (the time left, the end's reason in bits 0-1, the host's sent to
  all, then heard here too) and 0x00470740 (`net_clock_in`): the time left
  set; a reason ends the game: its line (0x9aa the time, 0x9ab the score
  limit, 5 s), the state 3, the end 3 s on. The game over itself
  (0x004707c0) is piece 8's. FIX (bug): 0x00470740 reads the game
  unchecked (no play change). Tests: `test_mech_net_world.py` (3000 events
  identical; a break caught) and the sender test's 0x11 (3000 sends). Not
  checked in a real mission: a crater (the headless c1m1 made none at the
  places tried, through the host or not).

- **Piece 8** (2026-10-31): the game's end. The host's rules check in the
  ping (0x0046fbd7..0x0046fc33: the CRC of the rules block +0x60 against
  +0x88, then the live +0x8c / +0x8d folded in for the next check) is left
  out, FIX (trap) (protection.md section 4). 0x004707c0
  (`net_game_over`, from the timer once the end state is 3 and its time
  passed): the local player's lobby kills and deaths (0x00435a30 /
  0x00435b40, -1 without a slot) onto the game's totals (+0x98 / +0x9c),
  the pilot written (its file opened, the header, the record, closed:
  `program_pilot_file`), the fade (0x0048ef00, 2 s, to black from the front
  buffer) to the Multi-Exit State (0x00605fa8, vtable 0x005c3680; its enter
  0x00470710 unloads the mission and replaces itself with the main menu,
  state 2), and the network end (0x0046d100). The main menu is where the
  original goes; the lobby (piece 11) is reached from there. Tests:
  `test_mech_net_end.py` (500 ends identical; a break caught) and
  `net_game_over_reaches_the_main_menu_c1m1_headless` (in a running c1m1
  the end fades to the Multi-Exit State and the main menu, the mission
  cleaned up; a break caught). A network game's main menu goes on to the
  Multiplayer State in the same frame (piece 8b found it).
- **Piece 8b** (2026-11-01): the soak test, before the lobby, through a
  debug start (ours, docs/program.md): `--mission c1mp1` with `--host` or
  `--join` launches a network game (type 3, the map's `readermp1`) with a
  stand-in game (no kill limit) and a stand-in lobby (each player its own
  team, its start by id, kills and deaths kept as the lobby would);
  `--net-wait N` holds the start until N players are in, `--net-minutes M`
  sets the clock, `--net-drive SEED` drives the pilot (the throttle up held,
  turned toward the nearest remote mech and firing when it is ahead within
  400 m, a turn and a straight walk out of anything it walks into, and every
  two minutes a kill staged on itself credited to the nearest remote mech:
  hits that land are far too few to kill in a soak), `--net-soak` prints a
  line each half second (the wall clock, memory, the mission clock, its
  own mech and every remote one), the scores at the end, and quits.
  `sdl_network_soak_four_players` (mw3sdltests, `MW3_SOAK=1`,
  `MW3_SOAK_MINUTES`, default 30; `MW3_SOAK_DIR` keeps the logs): four
  hidden processes under `nice` on loopback, 5% loss and 80 ms each way;
  each exits cleanly after the game's end on the clock, memory within 10%
  (the last tenth's median against the second tenth's), each sees the three
  others at the end, every remote mech within 5 m (95th percentile) of its
  owner's own track at the same moment (interpolated between its lines;
  deaths and respawns left out), the four score tables alike and not empty.
  The plan's counters of guaranteed messages and the convergence after the
  shim is off are left to the lobby's two-process test (piece 11). Found
  and fixed on the way: two players joining at once (ours, piece 4's
  transport): the second's welcome was made before the first had a player,
  and the host told only players already made of a new one, so the two
  never met; the host's answer to a player's creation (kId) now lists the
  players made since (`net_enet_two_joining_at_once_meet`, failing before).
- **Piece 8c** (2026-11-01, the lead's review): a peer's packet is
  untrusted over the internet; the exe trusted DirectPlay's peers. FIX (A)
  (fixes.md): a crater on a ground past the material table is dropped, and
  0x00536070 answers none past the table; an effect record's name is ended
  at its 32nd byte and its nodes (its own, a connector's one or two) must be
  -1 or live (0x004fe1d0 answers null for any negative index, as the exe:
  `saved_node` says so too); a state's shots stop at the message's end; a
  posture's kind outside 1..9 queues nothing. `net_hostile_peer_c1m1_headless`
  sends each of them, then 3000 random messages of every routed type at
  any length. The handlers still to come (chat text, names, the lobby's
  lists, pieces 9-14) take the same care: sizes, NUL-ended strings, indices.
- **Piece 8d** (2026-11-01, the soak's find): the pilot's respawn. The
  dead list (0x0041e220) asks the pilot's game's 0x0046dde0 once the pilot
  has been dead 7.1 s; the port's hook (`dead_calls().respawn`) was never
  set, so a dead pilot stayed dead (a soak's four mechs were all down from
  its second minute). Ported: 0x0046dde0 (`net_pilot_respawn`, set by
  net_start and cleared by net_end): respawn off (+0x8c) clears +0x8e and
  gives the view camera 4; on, the pilot's mech is made again (0x004783c0,
  `program_pilot_set_mech`: the place, the facing, the team and the fire
  mode kept, 0x004148c0 `mech_rebuild` in place, its loadout again
  0x00414430, the view back in its mode, the HUD reset, powered up), its
  team, unlimited ammo (+0x8d), the options (0x0040cb30), the cockpit's
  explosion reset, the "remote_N" start farthest from the nearest remote
  mech (0x0046e030, the squared flat distance), the lobby slot's spread,
  the place, 0x12, the sensor range, the objectives again (0x00491050,
  `Missions::objectives_again`), the force feedback. In the exe the
  stealth object takes the view while the mech is made again, so the
  weapons' adding asks no HUD of it; the port's target is none meanwhile
  (the stealth object is not made). Tests: `test_mech_net_respawn.py`
  (1500 respawns identical, 323 with respawn off; a break caught),
  `net_pilot_respawns_c1mp1_headless` (both branches in c1mp1), and the
  census in a network game, `program_hooks_set_in_a_network_game_headless`:
  every hook hook_allow.txt lets be null for "multiplayer:" must be set
  there (it fails with the respawn's hook unset). It found two such
  entries wrong: [0x0060c0c8] (the quicksand veto) and [0x0072fcac] (the
  shot gate) are read, null-checked, and never written by Mech3.exe, now
  "never set:".
- **Piece 9a** (2026-11-01): zNetwork's connections and the games' search
  over the transport (piece 9's plan). `znet::connections()` (0x005881f0)
  lists one provider by design, TCP/IP's kind 2 ("LAN / Internet (UDP)";
  9a first wrote kind 0, the modem's: the screen's switch 0x004949f0 has
  0 modem, 1 IPX, 2 TCP/IP, 3 serial);
  `find_games` (0x00586ff0 with 0x00587140) asks the transport for the
  connection's address (`Session::address`, "" the LAN) and keeps the
  answers as the game list (`Session::games`); `game_info` (0x00586ea0:
  an entry, or -1 the session) fills a `GameInfo` (the exe's 0x74 bytes:
  the four words, +0x10 the index, +0x14 the limit, +0x18 the players,
  +0x1c the name; the host sets bit 0 of the first word, the second is the
  game's version, the fourth 2 in a game in progress and 3 in a locked
  one); `open_host` (0x00586f60, 0x00587370) and `open_join`
  (0x00586fb0, by the entry's index). `Transport::find_sessions` /
  `stop_search`: the ENet transport's search is not blocking (a probe each
  second, the answers drained as they come); with no address the LAN's
  broadcast to the discovery port 27931, which a host now answers itself
  (`NetOptions::discovery_port`; the program's own DiscoveryAnswer is gone),
  with an address a probe to that host's game port, answered on the game
  socket before ENet reads it (ENet's intercept), so only UDP 27930 needs
  forwarding. The answer (protocol 2) carries the four words; a peer's
  answer is untrusted: another protocol, a cut frame, port 0, no limit,
  a limit past 64 or more players than it are dropped, the name cut at its
  first NUL and at 63 characters, control characters as '?'. Tests:
  `net_enet_search_finds_a_host_on_the_lan_and_by_address` (the LAN's
  probe to loopback in the test: a socket bound there hears no broadcast;
  a real broadcast on this Mac's LAN answered from 192.168.68.79),
  `net_enet_search_drops_broken_answers` (six broken answers and a good
  one; a break caught), `net_enet_znet_finds_and_joins_a_game`. Not a
  reflector (the lead's review): a probe is padded to 128 bytes, at least
  any answer (102 at most), shorter ones get none; each source address gets
  four answers a second (`net_enet_probes_are_padded_and_limited`: short
  probes on both ports unanswered, a burst of 20 answered 4 times, the next
  second again; each guard's break caught).
- **Piece 9b** (2026-11-01): the connection screen (`mech3/mp_connect`,
  MULTIPLAYER_DIALOG; the engine makes it as the Multiplayer State's first
  screen, 0x00493260). Its controls as the dialog binds them (offsets in
  the header), the modem's ANSWER, DIAL and CALL hidden (FIX (A)); the
  reset and the connect 0x004949f0 (TCP/IP's branch: HOST, the address,
  FIND GAME; the help lines 0x2a1 / 0x2a2 "Enter an IP address / Or leave
  blank to search", the original's own words for Owen's search and join
  by address), the update 0x00494470 (the search asked when its timer
  runs out: again at once while DPERR_CONNECTING, else in 3 s),
  SESSION_LIST's refill 0x00494510 ("%1 <Player Count: n of m>", "<Game
  in Progress>", "<Game Locked>", the chosen one kept), the buttons
  0x004953a0 (CANCEL the network off and back, JOIN 0x00495490, HOST
  0x00495600, FIND GAME 0x004957c0 turning into Stop Search, a game chosen
  0x00495410), the bars' focus 0x00495310, the hand-over 0x00494920 (the
  pilot's name, "<name>'s Game", 2..8 players with 8). The engine: the
  network switch 0x0047fc40 (on: zNetwork on the platform's transport,
  `Platform::network_open`, FIX (A); off: the game ended, zNetwork down),
  the page switch 0x0049cb20 and its request 0x0049cb00. Ours: the screen
  connects and searches the LAN as soon as it opens (Owen); Stop Search
  with another address searches there instead; a name in use is found
  after the join (DirectPlay listed a game's players before it), the game
  then left (`znet::close_session`). The game's version word is 0x28. 9a's
  note had TCP/IP as kind 0: it is 2 (0 the modem). Tests:
  `net_connect_screen_headless` (over the hub, which now answers the
  search: the hand-over's search, a line, another version left with 0xfde,
  a name in use with 0x2a9, a locked game not joinable, a good one joined
  with the lobby's page wanted, HOST refused without a name (0x2aa) then
  hosting; three breaks caught) and
  `sdl_two_processes_host_and_join_from_the_screens` (two mw3 through the
  menus: A hosts, B finds A's game in SESSIONS and joins; both reach the
  lobby's page). The program's `--net-discovery` / `--net-lan` point the
  LAN's search at loopback in the tests. Left for piece 11: the lobby
  (page 2), its pilot and game (0x00494920's second screen).
- **Piece 9c** (2026-11-01): internet hosting at Owen's "level 1".
  miniupnpc 2.2.8 vendored in `third_party/miniupnpc` (BSD-3-Clause,
  unmodified; the release tarball's sha256 05b929679091b9921b6b6c1f25e39e4c
  8d1f4d46c8feb55a412aa697aee03a93, every kept file identical to GitHub's
  miniupnpc_2_2_8 tag but for the CVS $Id lines; its notice in both
  THIRD-PARTY files, its licence in the Windows package).
  `frontend/sdl/net_upnp`: a `PortMapper` on a thread of its own (a router
  may take seconds or never answer) finds the router (SSDP; a description's
  URL in the tests) as the screens' network comes up, asks its public
  address, maps the game's UDP port when hosting (the HOST that succeeds:
  `Engine::network_hosting`) and removes the mapping when the network goes
  off or the game closes. Its states: looking, no router, ready, mapped,
  refused, carrier-grade NAT (100.64/10: the provider's; nothing mapped,
  nobody could reach it) and another router in front (a private "public"
  address: the port mapped on ours, the outer one left to the player). A
  transport bound to loopback asks no router (the tests), unless
  `--net-upnp URL` names one. The connection screen shows two lines under
  the host setup (ours, `CopyLine`): the address to give friends with
  "click to copy" (SDL's clipboard), or what stands in its way ("forward
  UDP 27930 on your router", "Your internet provider blocks hosting: use a
  LAN, or a VPN such as Tailscale", "Another router stands in front of
  yours: forward UDP 27930 on it, ..."), and this computer's LAN address
  (UPnP's, else the route's, `local_lan_address`). Tests: `net_upnp_*`
  against a fake router on loopback (its description, the SOAP calls: the
  mapping asked and removed, on unmap and with the mapper, a refusal,
  carrier-grade NAT with nothing asked, the double router mapped, no router
  quickly; three breaks caught) and `net_connect_screen_addresses_headless`
  (each state's line, the copies, HOST's mapping asked). A real router
  that answers with a private "public" address (a mesh behind the
  provider's box) gets the double-router line, not the provider's.
- **The soak's little damage** (the lead's question): the autopilot's, not
  the network's. At a known range the path works:
  `sdl_network_damage_point_blank` holds A 40 m from a still B
  (`--net-ahead 40`, `--net-staged 0`) firing 2 s in 7 for a minute; A's
  own shots hit B's network mech there (46 hits in a two-minute trial, no
  damage: its vt+0x164), B's machine replays them (0x004b08c0) and takes
  the same 46 hits, 36.8 damage, health 0.611; A's view of B comes back
  through the states' damaged parts (0.612). The autopilots seldom have a
  clear line: far apart, behind the ground (a stalled one 330 m off and
  34 m below its target), or circling. A break (no shots replayed) leaves
  B whole and fails.

- **Piece 11a** (2026-11-01): the lobby's frame (`mech3/mp_lobby`,
  MULTIPLAYER_HOST_DIALOG; the engine's `construct(0x495f90)`, entered by
  the page switch, given the pilot and its multiplayer game by 0x0049cc30).
  Its records are the multiplayer game's (pilot.h): a player's
  (0x00499030, vtable 0x005c7cc0: id, team, host, name, state 1 mech lab /
  3 ready, skin, its place in the count, kills, suicides, a score line) and
  a team's (0x00499280: name, players, skin, owner), made again from the
  network's walks (0x005898b0 / 0x00589910, which hand the callback the
  id and team as the exe's ECX / EDX) at each refresh; the lookups
  0x00495b10..0x00495d20 and the game's 0x00435910..0x00435b40 (a side in
  play is a player's place in the count, or in team play its team's
  skin), the rescore 0x004361e0 and the scores' lines 0x00435b60. A
  player's data, the lobby's kind 1: {1, skin, state, n, the add-on worlds
  it has}; enter (0x00497720) makes ours (outside team play every player
  starts with skin 1, 0x0049bd10 answering 0 there) and lists the worlds:
  UNAVAILABLE, the shipped eight (value number << 8 | folder) and every
  folder's `readermp*.zbd` numbered over 2 named by its mpinfo.zrd's
  db_name (c3's mp3 and mp4 in 1.2). The rules' lists and boxes set from
  the game; the handlers (2..6, 8..0xa the refresh, 7 kicked, 0xd a new
  host); the session's fourth word 1 again (open) through a new
  `set_game_info` (0x00586f20; the transport's `set_session_desc`, ENet's
  frame kDesc from the host). TP_LIST and PLAYERS_LIST (0x00498710): the
  host's "++", "**" for one without the chosen add-on world, "In MechLab",
  "Ready!", the skin's name, our mech's; SCORES_LIST (0x00498550) with
  the game service's report dropped (FIX (A), above); READY, MECHLAB,
  START, NEW_TEAM as things stand (0x004993f0), the host's rules controls
  (0x00498260), the lists' choices (0x0049a3a0, 0x0049a6a0), KICK and
  KICK2 (not ourselves, 0x2c1), being kicked (0x2c3, then CANCEL), CANCEL
  (the handlers off, back, the network off, the records gone). Ours: the
  host's two address lines (piece 9c's) at the screen's foot. Left for
  11b: the rules' changes and their message 0x20 (0x00499b10, 0x00499cf0),
  LOCK_OPTIONS (0x0049aca0) and the locked game refused at the door; 11c:
  START (0x0049b790, 0x1e), READY (0x0049a970), MECHLAB (0x0049b500), the
  teams' buttons (4, 5, 7) and PICK_SKIN, the scores' 0x14 (0x0049b150);
  12 the chat. Test: `net_lobby_screen_headless` (the host and one joiner
  on the hub: the worlds and our data, the lists' lines, the scores'
  columns and order, the session's word at the joiner, the kicks, being
  kicked); breaks caught: the host's mark, a world's value, the add-on
  check, the step over UNAVAILABLE, the scores' order, the players'
  places, the self-kick guard.
- **9c's quit** (the lead's review, 2026-11-01): the port mapper's end
  waits at most a second for its worker (a router still discovering, or
  stalling a SOAP call), then leaves it to finish on its own (it holds its
  state); removing the mapping stays best effort and the window closes.
  Test: `net_upnp_quit_does_not_wait_on_a_stalled_router` (the fake router
  holds AddPortMapping: the end takes 1.0 s; with a plain join 4.8 s,
  caught).
- **A real router's check** (the lead's): `MW3_UPNP_REAL=1
  MW3_TEST=net_upnp_real_router mw3sdltests`, skipped without the
  variable, no window needed (ssh). The mapper finds the router and maps
  UDP 27930; the test reads the entry back from the router itself
  (GetSpecificPortMappingEntry: to this computer, port 27930, "MechWarrior
  3"), unmaps and looks again (gone). It prints the states and the
  external address's class (public, private, carrier-grade NAT), never the
  address. Behind carrier-grade NAT it stops after the look (nothing is
  mapped there by design).

- **Piece 11b** (2026-11-01): the rules. The host's 0x20 with each
  refresh (0x00499b10: the flags 1 respawn, 2 mobile bases, 4 stock mechs,
  8 unlimited ammo, 0x10 first person, 0x20 locked, then the world, kills,
  minutes and tons as int16; guaranteed, to all); a joiner's (0x00499cf0):
  the lists set by value (a world we lack is UNAVAILABLE), the boxes set
  and disabled, the game's rules block (+0x60: the flags, the four
  values, a rand() word) and its CRC-32 at +0x88 (0x00576c10 from 0) kept
  as the exe does, read by nothing (the host's check stays dropped, FIX
  (trap), piece 8); the lock taken as sent, and on the first lock the
  players again and our mech fitted. FIX (A): only the host's, and whole. The rules' buttons (0xc: a world without MOBILE clears and
  disables it; 0xd..0x14 the refresh). LOCK_OPTIONS (0x0049aca0): not while
  a player lacks the chosen add-on world (0xfe0 with their names); asked
  first with neither a score nor a time limit (0xfbd), then 0x2c4; locked:
  the players again, the session's fourth word 3, the host's controls off,
  the rules sent, the mech fitted, the block kept (seeded with the time).
  The mech fitted (0x0049b360): the store's mechs out, each within the
  tons in (marked), a slot mech over the limit becomes the heaviest within
  it with its first stock loadout; with STOCK on a loadout not one of the
  mech's stock ones becomes the first, and the slot's is always a copy, so
  always: FIX (bug) behind `zfix bugs` (the lead), found by name instead. Ours: a locked game turns a joiner away
  at the door (ENet's kHello). The test hook sees message_box3 too
  (`last_box_text`). Tests: `net_lobby_rules_host_headless` (the messages
  and their fields, a change sent, the add-on refusal with the names, the
  endless-game question and a "no", the lock: word 3 at the joiner, the
  controls off, the block and its CRC, the heaviest of several mechs within 50 tons,
  the store cut), `net_lobby_rules_join_headless` (another player's and a
  short message ignored, unknown bits without effect, a world we lack, limits the
  lists lack, the lock with STOCK putting the second stock loadout back to
  the first) and `net_enet_session_desc_and_the_locked_door`. Breaks
  caught: any sender, no add-on check, the lightest mech (once several
  fit: at 30 tons only one does), the stock bit, no lock question, the
  open door.

- **Piece 11c** (2026-11-01): the start and each player's own. START
  (0x0049b790): the host tells everyone the players' places (0x1e: the
  player in each place 1..8; their order in the lobby's count) and shows
  the game in progress (its fourth word 2); everyone gives the game the
  lobby's settings (the score limit, 100 none: 0x7fffffff; respawn, ammo,
  first person, stock, mobile bases; the tons; the minutes as a float),
  the rules block's values (its flags or'd), the mission clock ([0x00609920]
  minutes x 60, [0x00609924] on unless unlimited), the world (folder +0x90,
  UNAVAILABLE -1; number +0x94) and the players' lines for the briefing
  (+0xac, 0x00436470: vt+0x10, with vt+0x04 "M U L T I P L A Y E R", vt+0x08
  the world's name, vt+0x0c the session's), drops the lobby's handlers and
  goes to the Load state (0x0049ca80), which now loads a multiplayer game's
  world (mp<number> in its folder). A joiner's 0x1e (0x0049bcb0) sets the
  places and starts on its next update; FIX (A) only the host's, whole.
  Ours: the network game's lobby calls (team_of 0x00435910, the start's
  place, a kill 0x004359b0 and suicide 0x00435ac0, the kills 0x00435a30 and
  the score toward the limit 0x00435a50, which mech_net had as one hook and
  are two now, the 0x14 list, the sets, the rescore 0x004361e0, a player's
  leaving +0x18) on this game in place of the debug start's stand-in
  (`program_net_lobby`). READY (0x0049a970): our data's state 3 or 2 (its
  check of another team with our skin calls 0x0049bd10 with the skin as the
  exception and never finds one: dead). MECHLAB (0x0049b500): our state
  kept, "In MechLab" (1), the lab on the game's store (stock mechs only with
  STOCK); back (0x0049b660, the engine's `multiplayer_resume`) our state
  again. The scores between games (0x14, 0x0049b150): each player's kills
  and suicides, the lines again; FIX (A) read only as far as the message
  goes. The teams: JOIN (0x0049a550, out of ours first), REMOVE an empty one
  (0x0049a630), a player into a team (0x0049a220: the first its owner, the
  team's data's first word), a new team with its skin (0x0049a300) and our
  skin (0x0049a850: our data's second word, or our team's); the last two are
  the new team's dialog's (piece 13, with NEW_TEAM and PICK_SKIN). The
  mission's mobile-bases flag ([0x00479480]+0xa8) from the game. The
  transports now tell the setter of its own data's change too, as of the
  teams' (ENet applies its own frame, the hub tells all): the exe needs it,
  its own READY refreshing its lines by the 9 it raises (0x00499600), or a
  host readying last would never see START. Tests:
  `net_lobby_start_host_headless` (READY's states and our "Ready!" line,
  the mech lab's state and back, the 0x14 scores and their line, START: the
  places at the joiner, word 2, the game's settings, world and roster, the
  network game asking this game, the briefing's texts, the handlers off),
  `net_lobby_start_join_and_teams_headless` (a new team joined with us its
  owner and its skin our side, its lines, another joined by JOIN, the empty
  one removed, our team's skin; another player's 0x1e ignored, the host's
  places, the start), `sdl_lobby_host_alone_starts_a_game` (one mw3 through
  the menus: HOST, LOCK and OK, READY, START, Beachhead loaded and ready to
  launch). Breaks caught: the world's folder and number swapped, any
  player's 0x1e taken, READY's states swapped, ENet's and the hub's setter
  left deaf, no team owner, no score hook, the Load state without
  multiplayer. Left: the chat (12), the new team's dialog (13), the briefing and debriefing
  (14), two processes through the lobby into a game.

- **Two processes through the lobby into a game** (the lead, after 11c):
  `sdl_two_processes_from_the_lobby_into_a_game`. A hosts from the menus,
  B finds and joins it; once both are in (`--click-players`, ours: a
  click N frames after the session first holds P players, so the two
  processes keep their order whatever their speed: a lock before B's
  arrival would now shut B out) A locks, says OK and readies, B readies, A
  starts. Both load c1 and reach play; `--net-soak` now also prints its
  line for a game the menus started ("soak menus"), and each machine sees
  the other's mech where the other puts itself (16 lines each, within
  2 m). The start places match the debug start's on c1mp1. Found while
  making it: nothing new between the lobby and the game.

- **The mobile bases' stations** (0x00490950, the lead, after 11c): with
  ALLOW MOBILE BASES the mission load (0x0048faf0's multiplayer branch)
  gives each point of the mission's waynet (waynet_mp<N>, the missions'
  +0xb0) a copy of the world's mfb_station01 (0x004f9af0(.., 0, 0)),
  added to the world, placed on the point, named mfb_station_04 on and
  shown; the point's label becomes that name (the dock asks by it). The
  emulator on the shipped waynet_mp files shows every element of a built
  waynet is a point with a place (c1's mp1: mfb_station_01 at -3104 -31
  -1502 and _02 at -1600 3 -1600; mp2: _03, _04), so the exe reads no
  null place; ported as it is (a placeless element would be passed over).
  The field base's repairs in a network game (0x00479480+0xa8) read the
  same flag (`program_set_mp_mobile_bases`). Test:
  `net_mobile_base_stations_c1mp1_headless` (c1mp1 as a network game: on,
  a station in the world on each point, shown, the points relabelled; off,
  none); breaks caught: the names from 01, not in the world, the flag off.

- **Piece 12** (2026-11-01): the lobby's chat. CHAT_REC_LIST (vtable
  0x005c6e70, `ChatLog`): a text box of 14 lines over the chat's last 100
  (a list of strings, the oldest dropped, 0x00495e60), a window between a
  top and a bottom line; a line added (0x004996d0) shown when the window
  was at the bottom (it moves with it), a full log at its top scrolled down
  a line first; CHAT_UP and CHAT_DOWN (0x004997c0, 0x00499820) scroll a
  line, writing the line come into view at the top or the bottom; the
  enter's clear (0x00499680) a log of three spaces. CHAT_ENTER_EDIT's Enter
  (0x00499880) sends to our team with CHAT_TEAM_CHECKBOX in team play,
  else to all (buttons 3 and 2, 0x00499a00 / 0x004999f0); the send
  (0x004998b0): the bar's text trimmed (empty: nothing), 0x1f {flags 1 to a
  team, the text with its 0}, its size the text's length and 0xc,
  unguaranteed (as the exe), to the team or to all; shown here as the
  others see it; the bar emptied. A player's (0x00499610): "< %1 to all >"
  (0xf44) or "< %1 to team >" (0xf43, 0x40 bytes at most), then the text;
  FIX (A): a message cut short dropped, the text ended within the message.
  The lead's check after 12: neither a peer's text nor its name is ever a
  format (the text goes in as "%s"; zutl::format_message fills the header's
  %1 in one pass over the message's own text, never over what it put in),
  and control characters become '?' in the text and in every name zNetwork
  records (players' and teams', `clipped`), as in the search's game names;
  tested with "%s%n%1" and an escape in a name and a text.
  Test: `net_lobby_chat_headless` (the message's bytes, our lines, an empty
  bar sending nothing, Bob's to the team, a short one and one without its
  0, 120 lines into 100, the window, UP and DOWN, a line added away from
  the bottom, the full log's step at its top, team play: to our team only,
  and to all again); breaks caught: no team flag, a short message taken, no
  step at the top, a window that does not move, Enter never to the team. A
  shot of the lobby with two lines sent shows them under their headers.

- **Piece 13** (2026-11-01): the new team's dialog, MULTIPLAYER_NEW_TEAM_DIALOG
  (`TeamScreen`, 0x0049bd80, the Multiplayer State's third page; the
  engine's `construct(0x49bd80)`, entered by the page switch with the
  lobby, 0x0049c2c0). Over a still of the screen under it (0x00579500(2),
  the 2D host's `screen_grab`, freed on OK or CANCEL, 0x00517b10). The
  lobby's NEW_TEAM (7, 0x0049a2b0: a new team, its skin, JOIN; the chat's
  bar without the keyboard, TP_LIST's choice off) and PICK_SKIN (8,
  0x0049a820: the skin alone) set its three flags (+0x111ec..+0x111ee) and
  open page 3. Enter: the name's bar (shown with the keyboard for a new
  team), SKIN_LIST on to choose, holding every skin no team has
  (0x0049bd10; all of them outside team play, our own team's taken too),
  SKIN_TEXTURES the chosen skin's "popup_box%d", a new team named by its
  first free skin's letter ("A Team", 0xf64), JOIN shown and checked. OK
  (0x0049c6c0): a new team needs a name (0x1e5) no team has (0x1e6,
  0x00589680, `znet::team_named`), then is made with the skin and joined
  with JOIN (0x0049a300); else our skin (0x0049a850); back (0x0049c680:
  the chat's bar the keyboard again, page 2). Enter in the name's bar is OK
  (0x0049bfe0); SKIN_LIST's change its image. Test:
  `net_lobby_team_dialog_headless` (A Team made and joined with skin 1,
  the next B Team without skin 1, a name in use and none refused, CANCEL,
  PICK_SKIN setting our team's skin 5); breaks caught: always "A", names
  repeated, taken skins listed, never joined, no skin set. A shot of PICK
  A SKIN over the lobby shows the still, the skin's picture and its list.

- **Piece 14a** (2026-11-01): the in-game chat. zTwoD's console
  (`ztwod::Console`, 0x005a40b0..0x005a4690, vtable 0x005d01a8; the interp
  debug console's too): a text box of 25 lines (Arial 10, white, at 160,
  100) over a box its layer ([0x00736130], a second weather-style layer)
  dims in green (0x004b5280 -> 0x0055a740) over 320 by its lines' height;
  a "> " prompt typed into (a line editor of 0x200 that cannot back over
  it; Enter its +0xd0, Escape the prompt alone, DIK_UP the line last
  entered); each key's click (snd_ricochet1 at 0.2); the game's commands
  off while it is up (a copy of the command map pushed, its +0x3f58 0,
  popped at its end) and the keyboard's characters its own (zInput's char
  callback). The chat panel (`ChatPanel`, 0x0041e5a0, vtable 0x005bd070):
  "CHAT ALL" or "CHAT TEAM" (0x11e, 0x11f) over the prompt, 0x29
  characters; its Enter (0x0041e660) the text after the prompt's '>'
  trimmed, sent unless empty (0x00470380), the panel closed. CHAT_TEAM
  (0x76) and CHAT_ALL (0x77) open it on the pilot's mech (0x0041e4e0, +0x378:
  in a network game only; to all without team play); the mech's going and
  the network's end close it (0x0041e580, the destructor's
  `player_object_free`); the HUD frame draws it (0x0041ab90, the engine's
  `mech_draw_cockpit`). The send (0x00470380): 0x1f as the lobby's, to our
  team (0x00495b60, NetCalls' new `lobby_team_id`) or to all, unguaranteed,
  shown here too. A line (0x00470490, the router's 0x1f): "<name " then "
  Send message to team" (0x899) or " Send message to all" (0x89a), "> ", the
  text, on the HUD for 5 s (0x0040fe20, NetCalls' new `hud_line`, given the
  text as a "%s" argument). As in the exe, a player without a network mech
  is not heard in a mission (the router). FIX (A): a message cut short
  dropped, the text ended within it, its control characters '?'. Test:
  `net_chat_panel_c1mp1_headless` (c1mp1 as a network game over the hub,
  Bob given a mech: CHAT ALL opened with its prompt and the commands off,
  " hi there " typed and Enter, sent to all and closed, the commands back,
  our line and Bob's "%s%n\x1b[" line, an empty line sending nothing,
  CHAT TEAM outside team play to all). A shot of the debug network start
  with CHAT_ALL's key and "hi" shows the panel over the cockpit. Breaks
  caught: the commands left on, the panel never closed, the text raw, an
  empty line sent, the team flag inverted.
- **Piece 14b** (2026-11-01): the multiplayer briefing, ported before (the
  briefing's 0x00405230 and 0x00408ef0), checked from the menus. A shipped
  world (number 1 or 2) takes its briefing.zrd section; an add-on world
  (number 3 and up, c3's mp3 and mp4: the game's +0x94 > 2) takes
  mpinfo.zrd's `briefing`. Only LAUNCH, pressed by itself once loaded
  (multiplayer_launch_prep first), so the screen shows for the load's few
  frames. Shots: Beachhead (c1mp1) with MULTIPLAYER, the world, the game's
  name and the players' line; Durgan City (c3mp3, picked with WORLD's
  arrow) from mpinfo.zrd. No code changed.
- **Piece 14c** (2026-11-01): the way back. A game's end (0x004707c0: the
  kills added to the pilot's totals, the pilot saved) fades to the
  Multi-Exit State (0x00470710: the mission unloaded, the main menu), whose
  enter (0x00486990) presses MULTIPLAYER while the network is up; the
  Multiplayer State's enter (0x0049cab0) wants the lobby's page
  (0x00586ce0), and the lobby's enter (0x00497720) fills SCORES_LIST from
  the last game's records before the refresh makes them again (so a later
  fill shows zeros, as the exe). There is no multiplayer debriefing: the
  lobby's scores are it. The game is open again (its enter's fourth word),
  so the host locks and starts as the first time. All of it was ported in
  pieces 9 to 11; new here: the dead list's `net_fade` (0x0041e220's
  [0x0060861c]+0x8c, ALLOW RESPAWN: a network wreck fades out over the second before 7.1 s, the respawn), set
  in the program and off the census's skips. Ours for the tests:
  `--net-seconds S` (each menu game's clock), `--net-kill-at S` (a kill
  staged S seconds of 30 frames in), `--net-ahead` in a menu game,
  `--click-return X,Y@N` (a click N frames after each return to the
  lobby), the "lobby entered N, scores: ..." line and the "soak menus game
  in / over" lines. Tests: `sdl_two_processes_lobby_game_and_back_twice`
  (two mw3 from the menus, three games of 20 s, A held 50 m from B and
  wrecked 8 s in; after each game both lobbies show "best 1 1 0 | test 0 0
  0"), `net_chat_panel_c1mp1_headless` (the wreck fades with ALLOW RESPAWN
  only). Breaks caught: the main menu not pressing MULTIPLAYER (one game
  only), the scores filled after the refresh (zeros), the fade's flag
  inverted. A first try clicked B's READY 250 frames after its return:
  B is back first, before A's lock reaches it, so its box was still off;
  400 frames is after it. Left for the census's owner: the player's death
  in a mission still reaches 0x0041b263, 0x004e6dc0, 0x0041d510,
  action.vt+0x3c and 0x00446ad0 (playtest.md's c3m2 note), in a network
  game as in single player. (0x0041d510 and action.vt+0x3c are now ported
  for the loss, below.) The 30-minute soak over this path (two mw3, 60 s
  games, a staged kill each): 111 games, the same scores on both after
  every one, but the memory rose from 206 to 513 MB. macOS's `leaks`
  found every mission's animations left behind (36 of 42 MB): the port's
  0x0050b380 wrote a dword over +0x08, which took the def count beside it
  (+0x0a, a word in the exe's store) to 0 before its loop, so it freed no
  def (a port slip, not the exe's: every mission change leaked its
  anim.zbd, campaign too). Now a word; the soak again: 103 games, memory
  flat (188 MB at the start, 157 at the end). Test:
  `missions_c1m1_c1m2_then_c2m1_in_one_process_headless` (every c1 def
  freed before c2; `zeff::anim_freed_defs`, ours). Break caught: the
  dword store.
- **14a's Enter, renamed** (the PC build's warning): the console's +0xd0
  was `on_enter`, the name of Object2D's +0x3c, the mouse routing's entry
  (Window::update, 0x005a2bb0), so it overrode it: the pointer coming over
  the panel would have sent the line and deleted the panel under the
  routing (the break crashes). Now `line_entered`; the chat test moves the
  mouse's entry over the open panel (it stays up, nothing sent).
- **The 1.2 read-me's six** (the lead, after 14c; game/mw3-1.2/readme.txt,
  the multiplayer changes of 1.1 and 1.2), each a test:
  1. 1.1's crash or lost keyboard when a player respawns while another's
     chat is open: held. Our panel up while Bob's mech is made again (0x12)
     and then while ours is (the respawn's remake, which closes it through
     `player_object_free`): the keys type, the text is sent, the commands
     come back (`net_chat_panel_c1mp1_headless`; break caught: the remake
     not closing the panel leaves the commands off, the old bug itself).
  2. 1.2's crash on fast uppercase letters and question marks: held. A
     burst of Shift and "MECHWARRIOR???" ten times (290 events, three of
     zInput's 0x80-event reads) through zInput and a ToAscii as Windows'
     into the lobby's chat bar and the in-game panel: no crash, each holds
     the burst as typed up to its length, the lobby sends it whole
     (`net_lobby_chat_headless`, `net_chat_panel_c1mp1_headless`).
  3. 1.2's weapon groups kept through a respawn: held. A weapon moved to
     another group writes it into the loadout's item (0x004b1e10's +0x60 on
     the item), which the respawn's remake configures from; after the
     network respawn (the dead list's 0x0046dde0) it is still in it, the
     others unchanged (`net_chat_panel_c1mp1_headless`; break caught: the
     item not written).
  4. 1.2's arms that flailed and mechs that ran in place on other screens:
     the running in place held; the arms needed a fix. Headless, Bob from
     his states (standing, walking at 10 m/s, stopped):
     `net_remote_pose_follows_the_states_c1mp1_headless`; two mw3, A
     walking from 5 to 15 s of a 30 s game and then slowing to a stop
     (`--net-walk`, ours): `sdl_two_processes_a_walk_seen_from_the_other`.
     On B's screen A's arms swung between their limits for about 6 s as A
     slowed: the aim's extrapolation (0x0046e4d0, by the latency over the
     gap since the last state, its four parts without a normalize) by a
     latency of 2^32 ms. The ping's 0x00470a00 loads the round trip less
     the peer's delay unsigned, so the delay past the trip (here, two
     processes' stepped clocks at different rates; in play a stall or a bad
     packet) is 24 days, not the 0 its clamp means: FIX (bug), behind bug
     fixes (fixes.md). Breaks caught: the unsigned load (2^32 ms; 38 jumps
     over 0.2 rad), the remote's throttle ignored (sliding without
     walking). A's own start is a step or two before it moves, on its own
     screen too; the test counts 1.5 s or more of a walking gait in place.
  5. 1.2's IP address from the numeric keypad: held on Windows, failed on
     a Mac. zInput takes the keypad's characters only with Num Lock on (as
     the exe from GetKeyboardState); SDL reports it off on a Mac, which has
     none, so the keypad gave the cursor keys' codes. FIX (A): the SDL
     host reports it on, on macOS (`sdl_keypad_types_an_address`; break
     caught: "###########").
  6. The read-me's "outside the mission boundaries in multiplayer your
     'Mech will blow up": failed, now ported. The mp worlds' objectives
     (reader.zbd, mp1 / mp2: an invisible fail on `outside_c1m1` and so on,
     set by the player's `reg_outside_*` region) end in the loss
     (0x0048f9e0), which in a network game sets the player's centre torso's
     internal structure to 0 (0x0041d510's part 1, its vt+0x3c with 0, 0)
     unless the mech is down already (+0x578 10). Both calls were census
     stand-ins; now the program's (and +0x578 read as the mech's animation
     state). Test: `net_chat_panel_c1mp1_headless` (the valve set, the mech
     down a frame on; break caught: the torso left alone).

- **The census's three** (the lead, after 14): the player's death on the
  HUD (0x0041b263), a destroyed weapon's voices (0x004e6dc0) and the
  objectives box's refill (0x00446ad0, which in a network game came from a
  kill's score: 0x00470170, not the death). Ported, with the HUD hooks'
  `multiplayer_game` (it answered 0) and their network scores and title;
  hud.md 24b. Two FIX (bug): the refill's null HUD read, the score lines
  as formats. A network game with a staged kill now reaches nothing
  unported.
- **The lobby's bottom row at 16:9 and the briefing's first frame** (the
  lead): resolution.md section 12.

- **16:9** (the lead, 2026-11-01, release.md): the three screens widened
  (resolution.md section 12): the connection screen (NET NAME, SESSIONS,
  CANCEL and JOIN left; CONNECT, HOST SETUP and HOST right; its 610x345
  lines lengthened at column 300), the lobby (GAME SETUP left, SCORES and
  CHAT in the middle, PLAYER SETUP right; CANCEL and LOCK OPTIONS left,
  MECHLAB in the middle, I AM READY and START right; its 640x480 lines at
  columns 202 and 412, the middle column widened), the new team's dialog
  one centred block, its backdrop (the canvas grabbed as it enters) left
  at 0. The briefing is one block on its widened map, as every briefing.
  Test: `menus_wide_spread_by_their_groups_headless`'s multiplayer line.
  Breaks caught: the backdrop moved, the lobby's table missing.
- **The mods check** (the lead, 2026-11-01; ours, no original): each
  player's kind-1 lobby data gains a block after its max(5, 4 + worlds)
  words: "MODS" (0x53444f4d), the gameplay hash's low and high words, the
  count (at most 16) and each name as a length (at most 64) and its bytes,
  four to a word. A reader reads it only as far as the data goes, the count and
  lengths capped (no block: hash 0, no names), so an original's data reads
  as no mods.
  The joiner checks the host's data once after entering (its box, then back
  to the connection screen, as a refused add-on map); LOCK compares every
  player's with ours before the add-on world check. Same hash or 0 on both:
  no change. Test: `net_lobby_mods_check_headless` (refused at join and at
  lock with both sides' names; equal hashes and two string-only mods pass).
  Breaks caught: the joiner's check off, LOCK's check off.
- **Mod worlds' maps** (the lead, 2026-11-01): the add-on scan in enter
  (0x00497720, folders 0 to 5) goes on to `mission_dir_count()`, skipping
  6 to 8 as the exe's bound did, so a mod world's `readermp<n>.zbd` maps are
  listed and travel in the data as `n << 8 | world`. Test:
  `net_lobby_lists_a_mod_worlds_maps_headless` (a mod world with a
  readermp3 listed last, named from its mpinfo, in our data). Break caught:
  the bound back at six.

## Open

- c3's two 1.2 multiplayer maps for a 1.0 install (patch12.md, What stays
  out): the lobby lists them when the files are there.
- The score report's service at [0x007307e0]: the Zone is the likely one
  (section 1's table); not needed (piece 11a).
- network-messages.md's open items: the caller of 0x21, 0x18's +4 value,
  the 0xf flag bits, whether the lobby handlers stay registered in a
  mission, the name of the setting at [0x00608e28].

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
| [0x007307e0] | an unidentified COM object, CLSID 3faf0afe-b48b-11d2-8a51-00c04f8ef4e9 (IID ...afd) | 0x00589c20 from 0x00589d00, used by the lobby screen (0x00498550); released at once when its set-up fails. **Open**: identify it before porting the lobby screen (not DirectPlay's own) |

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

## Open

- The COM object at [0x007307e0] (CLSID 3faf0afe-...): what it is and
  whether the lobby screen needs it.
- The names of dialog.zrd's three MULTIPLAYER dialogs and which screen
  uses each (read with the lobby screen's port).
- network-messages.md's open items: the caller of 0x21, 0x18's +4 value,
  the 0xf flag bits, whether the lobby handlers stay registered in a
  mission, the name of the setting at [0x00608e28].

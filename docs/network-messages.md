# Network messages (draft)

Mech3.exe 1.2, 2026-10-19. Follows `docs/network.md` (first look). Research
only; nothing here is ported yet. "Likely" marks a guess.

## Plumbing

- **Send**: 0x5888a0 `znet_send(ECX=msg, EDX=size, [esp+4]=guaranteed,
  [esp+8]=idTo)`, `ret 8`. Calls `IDirectPlay4::SendEx` (vtable +0xc4) with
  `idFrom` = local player (`[0x73075c]+4`), flags = 1 (`DPSEND_GUARANTEED`) if
  asked, plus 0x200 (`DPSEND_ASYNC`) when `[0x730765]` is set. `idTo = 0` is
  `DPID_ALLPLAYERS`, so almost every message is a broadcast to every peer. With
  async sends it returns the DirectPlay message id (0x588917).
- **Receive**: 0x588930 `znet_poll(ECX=max, DL=free_buffer)`. Loops on
  `IDirectPlay4::Receive` (+0x64) until the queue is empty or `max` messages
  were handled; `zGameApp::Run` passes ECX = -1 (0x58fdf0), so it drains
  everything each frame. The buffer `[0x7307d8]` grows on
  `DPERR_BUFFERTOOSMALL` (0x8877001e) with a log line. Messages under 4 bytes
  are dropped (0x5889ea). `idFrom == 0` (DirectPlay system) goes to 0x588a70;
  everything else to the dispatcher.
- **Dispatcher**: 0x587c40 `net_dispatch(ECX=sender DPID, EDX=msg)`. Walks the
  handler list at `[0x730794]` and calls every entry whose `uint16 type`
  matches the message's first word, `handler(ECX=sender, EDX=msg)`. Handlers
  are registered with 0x587aa0 `net_on(CX=type, EDX=fn, [esp+4]=2)`, removed
  with 0x587b20 (same type+fn), cleared with 0x587a00.
- **System messages become local messages**: 0x588a70 turns DirectPlay system
  messages into MW3-format messages and pushes them through 0x587c40, so game
  code sees one stream. Types 2-0xe below come from here (except 0xd, which is
  also a real wire message).
- **Remote mechs**: list `[0x605f84]`, keyed by the owner's DPID at mech
  +0x6fc. The in-game dispatcher looks the sender's mech up first.

Three registration sites:

| Where | Function | Types |
|---|---|---|
| zNetwork init 0x586bfc, 0x589bd5 | znet | 0xd -> 0x586d50 |
| Lobby (mission.cpp) 0x497fbf-0x4980b6 | | 2,3,4,5,6,8,9,0xa -> 0x499600; 7 -> 0x49a1d0; 0xd -> 0x49a200; 0x1f -> 0x499610; 0x20 -> 0x499cf0; 0x1e -> 0x49bcb0; 0x14 -> 0x49b150 |
| Mission start 0x46ceb0 (called from 0x405000) | | 0xf-0x1b,0x1f,0x21 -> 0x46d880; 6 -> 0x46d3b0; 1 -> 0x4d72a0; 0xe -> 0x4709d0 |
| 0x47fd68 (mission.cpp) | | 0xc -> 0x47fde0 |

0x46d880 (in-game dispatcher): type 0x19 goes straight to 0x46fde0. Otherwise
it finds the sender's remote mech; if there is none, only 0xf is accepted, and
it creates the remote mech (0x46d450) and then applies the 0xf to it. With a
mech it switches through the jump table at 0x46da28 (0x15, 0x1c-0x1e, 0x20 fall
to "ignore").

Most in-game sends go through one mech method, **Mech vtable +0x168 =
0x46f160** `net_event(kind, arg)` (vtable 0x5bcef8; slot 0x5bd060). `kind` is
the message type. It does nothing unless `[0x605fb4]` (network game running) is
set. Some messages use fixed templates in .data: 0x5e5608 (0x12, 16 bytes),
0x5e5618 (0x13, 12), 0x5e5628 (0xf, 60), 0x5e5668 (0x17, 8), 0x5e5670 (0x11,
12), 0x5e5680 (0x21, 16), 0x5e5698 / 0x5e56b8 (0x19, 28).

## Message list

"G" = guaranteed, "U" = unreliable. "All" = `DPID_ALLPLAYERS`.

### Local only (made by 0x588a70 or the net layer, never sent)

| Type | Size | From | Meaning | Handlers |
|---|---|---|---|---|
| 1 | - | nothing makes it | registered at 0x46d02a to 0x4d72a0, which just returns 0 (likely a leftover) | 0x4d72a0 |
| 2 | 8 | DPSYS_DELETEPLAYERFROMGROUP (0x588e83); +4 = pointer to the system message | player left a group (team) | lobby 0x499600 (UI refresh flag `[0x6099cc]`) |
| 3 | 4 | DPSYS_CREATEPLAYERORGROUP, player (0x588d7b) | a player joined; the net layer adds a 0xec-byte player record first | lobby 0x499600 |
| 4 | 4 | same, after 3, and for groups (0x588e4f) | player/group list changed | lobby 0x499600 |
| 5 | 4 | DPSYS_ADDPLAYERTOGROUP (0x588acf) | player joined a group (team) | lobby 0x499600 |
| 6 | 4 | DPSYS_DESTROYPLAYERORGROUP, someone else (0x588b03) | a player left | lobby 0x499600; in game 0x46d3b0: message 0x26a "%1 has bailed out of the game.", the remote mech is removed |
| 7 | 4 | same, the local player (0x588afa) | we were removed | lobby 0x49a1d0: dialog 0x2a8/0x2c3 "Network Problem" / "You have been kicked out of this game." |
| 8 | 4 | DPSYS_DESTROYPLAYERORGROUP, group (0x588c5a) | group gone | lobby 0x499600 |
| 9 / 0xa | 4 | DPSYS_SETPLAYERORGROUPDATA, player / group (0x588f09, 0x588f20) | data changed | lobby 0x499600 |
| 0xb | 4 | DPSYS_SETPLAYERORGROUPNAME (0x588f37) | name changed | (no handler registered) |
| 0xc | 4+len | net error path 0x58ae10 (text) | "Network Error:" + text (message 0xd78) | 0x47fde0 |
| 0xe | 8 | DPSYS_SENDCOMPLETE (0x588f5c); +4 = pointer to the system message | an async send finished | 0x4709d0: if its msg id is the local mech's last state packet (+0x66c), set +0x668 and store `dwSendTime` in `[0x605fc4]` |

DPSYS_SETSESSIONDESC (0x104) goes to 0x587740; DPSYS_SESSIONLOST (0x31) to an
error (0x588eea).

### On the wire

| Type | Size | Sent by | Flags/To | Payload | Handled by |
|---|---|---|---|---|---|
| 0xd | 4 | net layer: host after creating the session (0x58886c), host when a player joins (0x588da8), new host after DPSYS_HOST (0x588ed0) | G, all | none | 0x586d50: `[0x730760]` (host DPID) = sender. Lobby 0x49a200: host re-enables its settings controls (0x498260) |
| 0xf | 60 + n | each peer for its own mech, 0x46ef70 -> 0x46f160 | U, all | mech state, see below | 0x46e4d0 on the sender's remote mech |
| 0x10 | 20 | 0x46ef70 every 5.1 s, to one peer in turn (0x470ab0 cycles through the remote list) | U, one peer | +4 seq (`[0x5e5690]`++), +8 last seq received from that peer (+0x6e0), +0xc `[0x605fc4]`, +0x10 ms since that peer's last 0x10 | 0x470a00: latency, see Sync |
| 0x11 | 12 | host, 0x46ef70: every 5 s, and every frame once the game is ending | U, all | +4 bits 0-1 game-over reason, +8 time left (`[0x609920]`, float s) | 0x470740: sets the clock; reason 1 = "Game Over : Timeout" (0x9aa), 2 = "Game Over : Score Limit" (0x9ab); game ends 3 s later |
| 0x12 | 16 | 0x46dfbe (respawn path, after the mech model is rebuilt) | G, all | +4 position (3 floats, from 0x5a85e0) | 0x46ed30: if the remote mech is not local, rebuild it (0x46e0f0) and place it (0x418e90). Likely respawn |
| 0x13 | 12 | the dying mech, 0x41b14f (mech.cpp) | G, all | +4 victim DPID, +8 killer DPID (or own id) | 0x46eba0: "%1 has been killed by %2" / "%1 has managed to kill himself" (0xf09/0xf0a); the host then scores it (0x470170) |
| 0x14 | 6 + 12n | host, 0x470170, after each kill | G, all | +4 int16 count, then n x {DPID, kills, deaths} (likely; written by 0x4359f0/0x435b00) | in game 0x4708e0, lobby 0x49b150: score table |
| 0x15 | - | never sent alone | | kind 0x15 only sets a "section damaged" bit (+0x680) that rides in the next 0xf | registered, ignored |
| 0x16 | - | never sent alone | | kind 0x16 (weapon.cpp 0x4b35ab etc.) queues a fire event (+0x678) that rides in the next 0xf | 0x58b380 (returns 0) |
| 0x17 | 8 | 0x41683a (mech.cpp, inside the mech state setter 0x416780) | U, all | +4 state number | 0x46ed10: `0x416780(state, 1)` on the remote mech. Likely posture/animation state |
| 0x18 | 16 | 0x470320 from 0x423c04 (mech_anim area) | U, to the victim only | +4 int, +8 dx, +0xc dz (floats) | 0x4705b0 on the victim: records the attacker (+0x664), passes +4 to the mech's +0x39c object (+0x38), and adds dx/dz to its own position. Likely knockback from a hit |
| 0x19 | 28 | clients: crater hook 0x46fd30 (in `[0x60bf88]`); host: 0x4700e0, flag 0x80 set | G, all | +4 flags (bit 7 = from host), +8 model id, +0xc position, +0x18 size | 0x46fde0: clients add the crater only if bit 7 is set; the host checks it (0x4ffaa0) and, if accepted, broadcasts it again with bit 7 |
| 0x1a | 4 + n | effect hook 0x46feb0 (registered via 0x50d1d0) | G, all | an effect-animation record copied raw (0x50ca20 = its size); not sent for anims named `mfb_station_01` | 0x46ff50: starts the effect if it is not already running; `[0x605fb8]` stops it echoing back |
| 0x1b | 8 | 0x45a231 (mech_map.cpp) via 0x46ff90 | G, all | +4 index | 0x46ffd0 -> 0x45a140: looks up map object `index` and moves the mech there (0x45a180 -> 0x418e90). Likely a teleport/drop point |
| 0x1e | 36 | host, lobby 0x49b790 | G, all | 8 x DPID, one per slot | 0x49bcb0: each player's slot (+0x20) = index + 1 |
| 0x1f | 8 + len | in game 0x470380 (called from 0x41e724), lobby 0x4998b0 | U; to all, or to the sender's team group (0x495b60) | +4 bit 0 = team only, +8 text | in game 0x470490: "name: text" on the HUD. Lobby 0x499610: "< %1 to team >" / "< %1 to all >" (0xf43/0xf44) then the text |
| 0x20 | 16 | host, lobby 0x499b10 | G, all | +4 option bits 0-4 (5 check boxes), +8..+0xe four uint16 list choices | 0x499cf0: copies them into the lobby's controls |
| 0x21 | 16 | 0x46f160 kind 0x21 (caller not found) | U, all | +4 sender DPID, +8 target DPID, +0xc section index (or -1) | 0x46fff0: only the target acts: records the attacker and sets that section to 0 (`section->+0x3c(0,0)`). Likely "your part was shot off", from mech_parts code (0x460000) |

### The 0xf mech state (0x46f160, applied at 0x46e4d0)

Fixed part, 60 bytes:

| Off | What |
|---|---|
| +4 | position, 3 floats (0x5a85e0) |
| +0x10 | velocity x, y, z (+0x30) x 127, ftol to int16 (no clamp: above 258 m/s it wraps); the receiver multiplies by 1/127 (0.00787402) |
| +0x18 | heading, float: atan2(q.y, q.w) of the mech's yaw quaternion (+0x48), so half the yaw |
| +0x1c, +0x20 | the torso's turn (its quaternion +0x118 through 0x525f80): the first two angles |
| +0x24, +0x25 | signed bytes: the controls' throttle target (IControls vt+0x2c, +0x44) and turn (vt+0x24, +0x14), x 127 clamped to -127..127 |
| +0x26, +0x27 | not written (0 in the static buffer) |
| +0x28, +0x2c | the aim: the turn (quat_between (0, 0, -1) to the unit direction, then its log 0x5259c0) from the torso node's world position to the weapons' aim point (manager +0xc); its first two components |
| +0x30 | float, the distance to that aim point |
| +0x34 | the target's player (the target contact's mech, its +0x6fc; 0 without a target) |
| +0x38 | flag word, below |

The flags: bit 0 the collision query crossed something (+0xfc == 0), bit 1
inactive (+0x08 off), bits 2-6 the motion state (+0x578), bits 7-10 the
first queued action (+0x480's front), bit 11 limping (+0x5ac not 0; -1,
none, sets it too), bits 12-18 the shot count, bits 19-22 the damaged-part
count, bit 23 passive radar (+0xbc bit 2), bit 24 powered down or powering
down (vt+0xa0, 0x41cc10), bit 25 MASC on (+0x574), bit 26 the AMS on (its
first byte), bit 27 NARCed (+0x570 > 0), bit 30 jumping (the controls' jump,
vt+0x38, not 0), bit 31 crouched (+0x586). Bits 28 and 29 are never written.

Then one byte per shot since the last state (bits 0-3 the weapon's slot,
4-5 the manager's trigger state, bit 6 a locked target; the receiver fires
that weapon on the replica, 0x4b08c0), then one dword per damaged part: the
part id (1-8), then three bytes of 0-255 (1 is 255, 0 is 0, else ceil(f x
255)): the internal structure (vt+0x48) and the armour (vt+0x44) of the
part's section (7 and 8 are the torso's sections 1 and 2), and the armour
of section + 3 (the rear). The count covers bits 0 and 9 of +0x680 too,
which are never written: their words are sent as zeros.

Every 26th state packet sets all eight section bits (0x46efc4, `+0x680 =
0x1fe`), so the full damage state is resent about every 4-13 s in case
unreliable packets were lost.

Send rate, 0x470b40 (seconds between packets), from `[0x608e28]` (a lobby
setting written at 0x494bc4-0x494de9; likely connection speed) and the player
count (mission +0x50):

| `[0x608e28]` | Interval |
|---|---|
| 1 | 0.1 s (10 Hz) |
| 0 or 2 | by players: 2-4 -> 1/7 s (7 Hz), 5 -> 0.2, 6 -> 0.25, 7 -> 0.286, 8 -> 0.333 (3 Hz), other -> 0.5 |
| anything else | 0.25 s |

## Sync model: state/event sync, not lockstep

Evidence:

- No frame numbers or input records in any message. The 0xf packet carries
  results (position, velocity, heading, damage values) and discrete events
  (weapon fired, state changed).
- The poll (0x588930) drains whatever has arrived and returns; nothing waits
  for peers. The main loop calls it once per frame (0x58fdf3).
- State packets are unreliable (`guaranteed = 0` at 0x46f972) and sent on a
  timer (2-10 Hz), not per frame. Lost packets are just replaced by the next
  one, and damage gets a periodic full resend.
- The receiver extrapolates: 0x46e4d0 scales the change since the last packet by
  `(latency + dt) / dt` (remote mech +0x6f8 latency, +0x73c / +0x6d8 times).
  That is dead reckoning, which lockstep would not need.
- Latency is measured with 0x10 pings, NTP style (0x470a00):
  `one_way = (now - sent - peer_hold - (our_send_queue - echoed)) / 2000` s,
  clamped at 0. The async send-complete time (0xe -> `[0x605fc4]`) is taken
  off, so time in DirectPlay's own queue is not counted as lag.

Who decides what:

- **Each peer owns its own mech**: movement, weapon fire, its own damage values
  (the 0xf damage records overwrite the replica), its own death (0x13 is sent
  by the victim, 0x41b14f) and its respawn (0x12).
- **Shooter -> victim, direct**: knockback (0x18) and shot-off parts (0x21) go
  to or are acted on only by the victim's machine. So damage from hits is
  likely applied on the victim's machine, from its copy of the shooter's fire
  events (fire events replay on the replica, 0x4b08c0).
- **Host** (`[0x730760]`, checked by 0x586d20): the game clock and game over
  (0x11), the scoreboard (0x14, from kill reports), lobby settings (0x20) and
  slots (0x1e), and craters (0x19: clients ask, the host checks and
  rebroadcasts). The host is a DirectPlay player like the others: messages go
  peer to peer to all players, not through the host.

## Discoveries

- **If the host leaves, the game picks a new host instead of ending.**
  DirectPlay's DPSYS_HOST message (0x588ebb) marks the local player as host and
  it tells everyone (0xd).
- **Your own machine decides whether your mech dies.** The victim reports its
  own damage and death (0xf damage records, 0x13), and the host only counts the
  kill. Nothing checks it, so a modified client could likely ignore hits.
- **The host checks the game rules for tampering.** About every 51 s (every
  10th ping, 0x46fbd7) the host takes a CRC-32 (0x576c10) of a 16-byte rules
  block (mission +0x60, which includes a random number from session setup,
  0x49a0b4) and compares it with the value stored at setup (+0x88). On a
  mismatch it ends the game (0x4707c0). Odd detail: right after the check it
  may set bits 0 and 3 of that same block (from +0x8c/+0x8d), which would fail
  the next check. Likely deliberate, a way to end the game at the next check.
- **Fewer players, faster updates.** Your mech's position goes out 7 times a
  second with 2-4 players, dropping to 3 with 8. One setting makes it 10. Eight
  players is the most the tables handle (rate table, 8 slots in 0x1e).
- **Craters are shared**, and only the host decides which ones exist (0x19).
  Effect animations (0x1a) are shared too, except `mfb_station_01`.
- **Team chat is real team chat**: it goes only to your team's DirectPlay
  group (0x495b60), not to everyone with a "hide" flag.
- **"60 Seconds To Go!"** (0xf0b) shows when the host's clock passes 60 s left
  (0x46f05d); the game ends 3 s after the host says time or score is up.
- Bailing out shows "%1 has bailed out of the game." (0x26a); being dropped
  shows "You have been kicked out of this game." (0x2c3).

## Open

- The caller of kind 0x21, and what 0x18's +4 value is.
- Exact meaning of the 0xf flag bits and the +0x1c..+0x30 fields (needs the
  mech struct, step 2 in CLAUDE.md).
- Whether the lobby handlers stay registered during a mission (no 0x587b20
  calls were seen for them), which would mean 0x1f and 0x14 run both handlers.
- What `[0x608e28]` is called in the lobby UI.

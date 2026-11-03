# Cheat codes (ours)

The rebuild's own cheat codes (Owen, 2026-11-01). The original has none a
player can type: its cheats are the options' switches that set the cheated
flag [0x00609994] (protection.md), which marks a campaign mission as not
counted. These codes leave that flag alone. Code: `mech3/cheats` (the
codes, the box, the key) and `mech3/app_cheats.cpp` (the program's side).

## Where they work

Single player only: the campaign, Instant Action and training. In a network
game (0x00479480, the check the chat keys make) the box does not open and a
code does nothing, and Keypad \* and Keypad − stay CHAT_ALL and CHAT_TEAM.

## The box

- **Keypad \*** (CHAT_ALL, 0x77) and **Keypad −** (CHAT_TEAM, 0x76), which
  do nothing outside a network game in the original (0x0041e4e0 returns),
  open it on the player's mech, in the chat panel's place (Mech +0x378).
- **Shift+Enter** (key code 0x41c) is our command 0x9d, bound at each
  mission's load (0x0040db60) when no command has the key; Enter alone stays
  NEXT_WEAPON (0x27). 0x9d is one of the exe's debug commands no key or file
  binds (0x0040dba0's 0x82..0xa4).
- It is the chat panel's console (zTwoD's, 0x005a40b0) titled CHEAT: the
  game's commands are off while it is up (the console pushes a disabled copy
  of the command map), the mission keeps running, Enter takes the line,
  Escape closes it (the chat panel's Escape only clears the line).
- Capitals and extra spaces are ignored. A line that is no code closes the
  box with nothing done and no message.
- A code shows "Cheat enabled: <code>" (or "disabled") on the message panel
  for 5 s (0x0040fe20) and writes the same line to the log.

## The codes

| Code | Effect | How |
|---|---|---|
| black sheep wall (toggle) | Every mech and vehicle on the radar at any range | The radar's contact pass (0x0043dd00, hud.md section 15) draws every contact, not only those in sight, then every mech and vehicle on zEntity's list with no contact, alive and not hidden, placed by its distance (clamped to the radar's edge like the contacts). The sensors and the AI are untouched: nothing new can be targeted |
| power overwhelming (toggle) | No damage to the player's mech | Mech +0x630 (invincible), as the mission scripts set it |
| operation cwal (toggle) | The player's reload and heat dissipation 10x | Weapon::update (0x004b2c20) counts the reload down by 10 x the frame's time; the heat (0x004192f0) multiplies the dissipation by 10 after the sinks' rate |
| food for thought (toggle) | Maximum salvage | Both of the salvage's coin flips always won (below) |
| there is no cow level | The mission won | Maximum salvage on; every enemy mech and vehicle still up (not turrets or buildings) killed by the player with one of its own shots each (zWeapon's impact, so the kill credit 0x0041b420 and its salvage run as in play), each salvaged as if undamaged; every primary and secondary objective done (0x0045e110, with its salvage); Missions::complete (0x0048f740) |
| game over man | The mission lost | Missions::lose (0x0048f9e0), as the player's death |

Every code lasts for the mission it was typed in: the mission's load
(0x004e7b40's listeners) turns them all off. Typing a toggle again turns it
off; turning power overwhelming off clears the mech's flag even if a mission
script had set it.

## Maximum salvage

The salvage (campaign.md, the salvage) has two coin flips (rand() & 1, 50%):

- a destroyed enemy mech that lost a leg comes to you whole only on a coin
  (0x00472f00); with maximum salvage it always does;
- a destroyed enemy vehicle's weapons come to you only on a coin
  (0x00472ec0); with maximum salvage they always do.

What the fight destroyed stays lost: a mech whose centre torso was destroyed
(not by a head shot) is not salvaged, and equipment in destroyed parts is
not either, as in the original. The salvage still comes only from kills
credited to you or your lance in the campaign (0x0041b420, game type 1).

There is no cow level's kills go further: each enemy is salvaged as if
undamaged (no part destroyed), so every enemy mech comes whole with all its
equipment.

## Tests

`mission_c1m2_cheat_codes_headless` (the keys, the box, each toggle's
effect, game over man's loss, none on in the next mission),
`mission_c1m2_no_cow_level_headless` (the kills, the salvage, the
objectives, the debriefing as a win), `cheat_codes_parse_and_refuse_in_a_network_game`
and `cheat_food_for_thought_wins_the_salvage_coins` (test_mech.cpp).

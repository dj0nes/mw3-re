// The hooks the program leaves unset where the port skips the call (or
// takes 0 / null / false for its answer): each gets a stand-in that tells
// the census, under the exe address it stands for, and returns the same.
// Kept by hand from tools/analysis/hookaudit.py (hooks-audit.md);
// put in at each mission's start and frame (MissionHooks::hooks_check).
// SKIP(accessor, field, key)
SKIP(mech3::collide_calls, ff_jolt, "0x004335d0")  // CollideCalls.ff_jolt
SKIP(mech3::damage_calls, equip_voice, "0x00485390")  // DamageCalls.equip_voice
SKIP(mech3::damage_calls, net_event, "0x0046f160")  // DamageCalls.net_event
SKIP(mech3::damage_calls, ff_hit, "0x00433720")  // DamageCalls.ff_hit
SKIP(mech3::damage_calls, voice, "0x00485370")  // DamageCalls.voice
SKIP(mech3::damage_calls, zaps_hud, "0x0043be70")  // DamageCalls.zaps_hud
SKIP(mech3::dead_calls, net_update, "0x0046ef70")  // DeadCalls.net_update
SKIP(mech3::frame_calls, net_entity, "FrameCalls.net_entity")  // FrameCalls.net_entity
SKIP(mech3::frame_calls, net_place, "FrameCalls.net_place")  // FrameCalls.net_place
SKIP(mech3::free_calls, player_object_free, "0x0041e580")  // FreeCalls.player_object_free
SKIP(mech3::heat_calls, heat_haze, "0x0043c1f0")  // HeatCalls.heat_haze
SKIP(mech3::heat_calls, hud_message, "0x0040fe70")  // HeatCalls.hud_message
SKIP(zdec::hooks, register_restore, "Hooks.register_restore")  // Hooks.register_restore
SKIP(mech3::mech_hud_calls, zoom_camera, "MechHudCalls.zoom_camera")  // MechHudCalls.zoom_camera
SKIP(mech3::move_calls, controls_landed, "0x005ac5c0")  // MoveCalls.controls_landed
SKIP(mech3::move_calls, ff_landing, "0x00433800")  // MoveCalls.ff_landing
SKIP(mech3::part_calls, footstep_ff, "0x004337c0")  // PartCalls.footstep_ff
SKIP(mech3::pocket_calls, net_element, "0x00609980")  // PocketCalls.net_element
SKIP(zwep::run_hooks, net_detonated, "0x0072fcb4")  // RunHooks.net_detonated
SKIP(mech3::update_calls, net_update, "0x0046ef70")  // UpdateCalls.net_update
SKIP(mech3::weapon_calls, net_event, "WeaponCalls.net_event")  // WeaponCalls.net_event
SKIP(mech3::weapon_calls, ff_weapon, "0x00433630")  // WeaponCalls.ff_weapon
SKIP(mech3::weapon_calls, target_projectile, "WeaponCalls.target_projectile")  // WeaponCalls.target_projectile
SKIP(zcls::world_render_hooks, after_scene, "0x005a67c0")  // WorldRenderHooks.after_scene
SKIP(zcls::world_render_hooks, dirty_rect, "0x00576ce0")  // WorldRenderHooks.dirty_rect
SKIP(mech3::damage_calls, player_died_hud, "0x0041b263")  // DamageCalls.player_died_hud
SKIP(mech3::dead_calls, net_fade, "DeadCalls.net_fade")  // DeadCalls.net_fade
SKIP(mech3::draw_calls, map_point, "0x004586e0")  // DrawCalls.map_point
SKIP(mech3::pocket_calls, view_pocket, "0x004b5290")  // PocketCalls.view_pocket
// The mission's network side (mech_net.cpp): pieces 5b-7 and the lobby.
SKIP(mech3::net_calls, state, "0x0046e4d0")  // NetCalls.state
SKIP(mech3::net_calls, ping, "0x00470a00")  // NetCalls.ping
SKIP(mech3::net_calls, clock, "0x00470740")  // NetCalls.clock
SKIP(mech3::net_calls, respawn, "0x0046ed30")  // NetCalls.respawn
SKIP(mech3::net_calls, kill, "0x0046eba0")  // NetCalls.kill
SKIP(mech3::net_calls, scores, "0x004708e0")  // NetCalls.scores
SKIP(mech3::net_calls, rules, "0x0058b380")  // NetCalls.rules
SKIP(mech3::net_calls, posture, "0x0046ed10")  // NetCalls.posture
SKIP(mech3::net_calls, knockback, "0x004705b0")  // NetCalls.knockback
SKIP(mech3::net_calls, effect, "0x0046ff50")  // NetCalls.effect
SKIP(mech3::net_calls, drop, "0x0046ffd0")  // NetCalls.drop
SKIP(mech3::net_calls, chat, "0x00470490")  // NetCalls.chat
SKIP(mech3::net_calls, part_off, "0x0046fff0")  // NetCalls.part_off
SKIP(mech3::net_calls, crater, "0x0046fde0")  // NetCalls.crater
SKIP(mech3::net_calls, send_done, "0x004709d0")  // NetCalls.send_done
SKIP(mech3::net_calls, create, "0x0046d450")  // NetCalls.create
SKIP(mech3::net_calls, reckon, "0x0046ed70")  // NetCalls.reckon
SKIP(mech3::net_calls, lobby_left, "0x00495b10")  // NetCalls.lobby_left

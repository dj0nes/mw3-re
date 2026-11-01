#pragma once
#include <cstdint>

#include "gamez/zgame/unported.h"

// C:\zipper\Mech3\player.cpp (0x0049dc80..0x0049fa20, and the vehicle
// builders after it to 0x0049ff80, which name the same file in their
// reports): the mission's vehicles made from aiv.zrd as the mission starts
// (docs/mission-start.md), and the player's per-frame tick. Everything
// outside player.cpp is a hook; the AI's vehicle records (mech3/zrdraiv) are
// read through hooks too, so the tests can feed them.
namespace mech3 {

namespace ai { class AivRecord; }
using zgame::Handle;

// The calls out of player.cpp, by the original's address. Objects the port
// does not model are handles; "silent" methods read their fields and are not
// calls in the original (the tests answer them from a model of its memory).
class PlayerHooks : public zgame::Unported {
 public:
  // The AI nets and their list (mech3/zainetedit, 0x004d52b0 / 0x004d5300 not ported).
  virtual void nets_build_all() { unported("0x00402170", {}); }
  virtual void nets_begin() { unported("0x004d52b0", {}); }    // every net's vt+0x28 -> vt+0x2c
  virtual void nets_end() { unported("0x004d5300", {}); }      // and vt+0x30
  // zClass: the player's tick object.
  virtual Handle object3d_create() { return unported("0x004fb740", {}); }
  virtual void set_action_priority(Handle node, int32_t p) { unported("0x004eb5b0", {node, p}); }
  virtual void set_action(Handle node, uint32_t fn) { unported("0x004eb450", {node, fn}); }
  // zReader. Silent: a list node's item i, as a float or as the item itself.
  virtual Handle reader_load(const char* path) { return unported("0x00556740", {path, 0, 0}); }
  virtual Handle reader_find(Handle node, const char* key) { return unported("0x005569e0", {node, key}); }
  virtual void reader_free(Handle node) { unported("0x00556820", {node}); }
  virtual float reader_item_float(Handle node, int32_t i) = 0;
  virtual Handle reader_item(Handle node, int32_t i) = 0;
  // The pilot ([0x0060861c]) and its games.
  virtual int32_t game_type() { return static_cast<int32_t>(unported("game.vt+0x2c", {})); }
  virtual Handle campaign() { return unported("0x00479440", {}); }   // the current game when a campaign
  virtual Handle training() { return unported("0x004794a0", {}); }   // when training
  virtual Handle training_slot(Handle training) { return unported("training.vt+0x34", {training, 0}); }
  virtual void slot_set_config(Handle slot, Handle config) { unported("0x00433df0", {slot, config, 0, 0}); }
  virtual Handle lancemate_init(Handle mem, int32_t mech) { return unported("0x00433cc0", {mem, mech, 0, 0}); }
  virtual void campaign_set_lancemate(Handle c, int32_t i, Handle slot) { unported("0x004351c0", {c, i, slot}); }
  virtual void campaign_set_lancemates(Handle c, int32_t n) { unported("0x00435230", {c, n}); }
  // The launch's (0x0049e5e0): the pilot's game's slot i (vt+0x34; 0 the
  // player's), the campaign's slots (vt+0x34) and their count (vt+0x38).
  virtual Handle game_slot(int32_t i) { return unported("game.vt+0x34", {i}); }
  virtual Handle campaign_slot(Handle c, int32_t i) { return unported("campaign.vt+0x34", {c, i}); }
  virtual int32_t campaign_slot_count(Handle c) { return static_cast<int32_t>(unported("campaign.vt+0x38", {c})); }
  // aiv.zrd (mech3/zrdraiv): the record object's reads, and the file's count.
  virtual ai::AivRecord* aiv_read_named(ai::AivRecord& rec, const char* name);  // 0x004e0680
  virtual ai::AivRecord* aiv_read(ai::AivRecord& rec, int32_t i, const char** name);  // 0x004e0600
  virtual int32_t aiv_count();                                                   // 0x004decf0
  // The mech definitions (mech3/mech_reader): mech_dfn's configurations and
  // a mech's skins, as vectors (silent).
  virtual Handle mech_dfn(int32_t mech) { return unported("0x004846a0", {mech}); }
  virtual int32_t dfn_config_count(Handle dfn) = 0;           // dfn +0x20 -> +0x0c..+0x10
  virtual Handle dfn_config(Handle dfn, int32_t i) = 0;
  virtual Handle mech_dfn_of(Handle mech) { return unported("0x0041e9a0", {mech}); }
  virtual int32_t dfn_skin_count(Handle dfn) = 0;             // dfn +0x1c -> +0x04..+0x08
  virtual Handle dfn_skin(Handle dfn, int32_t i) = 0;
  virtual int32_t mech_index(const char* name) { return static_cast<int32_t>(unported("0x004845f0", {name})); }
  // A vehicle's AI net by its number: znet's object, its vt+0x28, then +0x34.
  virtual Handle znet_find(int32_t id) { return unported("0x0058b160", {id}); }
  virtual Handle net_object(Handle o) { return unported("netobj.vt+0x28", {o}); }
  virtual Handle net_compiled(Handle o) { return unported("netref.vt+0x34", {o}); }
  // Memory and objects not ported.
  virtual Handle alloc(uint32_t size) { return unported("new", {size}); }
  virtual void set_field32(Handle h, uint32_t off, uint32_t v) = 0;  // silent
  virtual uint32_t field32(Handle h, uint32_t off) = 0;              // silent
  // The AI's controls (controls.md): a mech's (0x320 bytes) and a vehicle's
  // (0xcd8, through 0x004ac860); their IControls virtual base (silent), its
  // deleting destructor and vt+0x90.
  virtual Handle ai_mech_controls(Handle mem, Handle net, const ai::AivRecord* rec) {
    return unported("0x004d8750", {mem, 0, net, rec_handle(rec), 1});
  }
  virtual Handle alloc_4ac860(uint32_t size) { return unported("0x004ac860", {size}); }
  virtual Handle ai_vehicle_controls(Handle mem, Handle net, const ai::AivRecord* rec) {
    return unported("0x004e71c0", {mem, 0, net, rec_handle(rec), 1});
  }
  virtual Handle controls_interface(Handle ctl) = 0;                 // ctl + vbtable[1] + 4
  virtual void controls_delete(Handle ictl) { unported("ictl.vt+0x0", {ictl, 1}); }
  virtual Handle controls_vt90(Handle ictl) { return unported("ictl.vt+0x90", {ictl}); }
  // The mechs (mech.cpp).
  virtual Handle mech_create(int32_t mech, Handle ctl, int32_t skin, const char* name) {
    return unported("0x00414150", {mech, ctl, skin, name});
  }
  virtual void mech_set_skin(Handle mech, Handle skin) { unported("0x00414390", {mech, skin}); }
  virtual void mech_set_config(Handle mech, Handle config) { unported("0x00414430", {mech, config}); }
  // Silent: the mech's roster number, [[mech +0x684] +0x0c].
  virtual int32_t mech_roster(Handle mech) { return static_cast<int32_t>(field32(field32(mech, 0x684), 0x0c)); }
  virtual int32_t mech_place(Handle mech, const ai::AivRecord* rec) {  // (x y z), yaw
    return static_cast<int32_t>(unported("0x004161c0", {mech, rec_handle(rec) + 8, rec_yaw(rec)}));
  }
  virtual void mech_delete(Handle mech) { unported("mech.vt+0x0", {mech, 1}); }
  virtual void mech_update(Handle mech, float dt) { unported("mech.vt+0x34", {mech, zgame::bits(dt)}); }
  virtual void mech_power(Handle mech, int32_t down) { unported("0x0041cc30", {mech, down}); }
  // Other vehicles: the mobile HQ (0x004589d0) and the basic vehicle
  // (veh_basic.cpp, 0x004ac320), the entity name, their virtuals.
  virtual Handle mobilehq_create(Handle ctl, const char* type, const char* name) {
    return unported("0x004589d0", {ctl, type, name});
  }
  virtual Handle vehicle_create(Handle ctl, const char* type, const char* name) {
    return unported("0x004ac320", {ctl, type, name});
  }
  virtual void entity_name(Handle v, const char* name) { unported("0x005a84c0", {v, name}); }
  virtual void vehicle_place(Handle v, const ai::AivRecord* rec) {
    unported("veh.vt+0xdc", {v, rec_handle(rec) + 8, rec_yaw(rec)});
  }
  virtual void vehicle_setup(Handle v, Handle common, const char* name, int32_t kind) {
    unported("veh.vt+0x158", {v, common, name, kind});
  }
  virtual void vehicle_anims(Handle area, Handle node, Handle mode, const char* name, const char* type, Handle slot) {
    unported("0x0049ef40", {area, node, mode, name, type, slot});
  }
  virtual void vehicle_49f660(Handle v) { unported("0x0049f660", {v}); }
  virtual void vehicle_touch(Handle v, Handle node) { unported("0x005833b0", {v, node, 1, 0x4af550}); }
  virtual void vehicle_5a85c0(Handle v) { unported("0x005a85c0", {v}); }
  // A vehicle's own net (vt+0xd8, 0x004d70f0: ai::vehicle_net_of), the
  // lancemates'.
  virtual Handle vehicle_net(Handle v) { return unported("pveh.vt+0xd8", {v}); }
  // Silent: a vehicle's controls (+0x37c) as their IControls virtual base.
  virtual Handle vehicle_controls(Handle v) {
    const Handle c = field32(v, 0x37c);
    return c ? c + 4 + field32(field32(c, 4), 4) : 0;
  }
  // 0x0049e4e0 (ECX the player's settings): the record's region triggers to them.
  virtual void take_triggers(Handle settings, const ai::AivRecord* rec) {
    unported("0x0049e4e0", {settings, rec_handle(rec)});
  }
  // Instant Action's (0x0049dc80): the game (0x00479460), its line-up
  // (ActionGame: vt+0x34, 0x00436a90, 0x00436ac0, 0x00436840), a slot's
  // mech (+0x0c, 0x00433ee0) and paint (+0x10, 0x005a2b90); silent: the
  // game's mode +0x44, field bases +0x48, wave +0x6c, slots +0x7c and a
  // slot's +0x04, +0x08.
  virtual Handle action_game() { return unported("0x00479460", {}); }
  virtual Handle action_slot(Handle g, int32_t i) { return unported("action.vt+0x34", {g, i}); }
  virtual bool action_ally(Handle g, int32_t i) { return (unported("0x00436a90", {g, i}) & 0xff) != 0; }
  virtual void action_drop_enemies(Handle g) { unported("0x00436ac0", {g}); }
  virtual Handle action_add(Handle g, int32_t mech, uint8_t side, Handle config, int32_t paint) {
    return unported("0x00436840", {g, mech, side, config, paint});
  }
  virtual void slot_set_mech(Handle slot, Handle mech) { unported("0x00433ee0", {slot, mech}); }
  virtual void slot_set_paint(Handle slot, int32_t paint) { unported("0x005a2b90", {slot, paint}); }
  // mech_names: a roster mech's weight class and name (`notmech` out of range).
  virtual int32_t weight_class(int32_t mech) { return static_cast<int32_t>(unported("0x00484700", {mech})); }
  virtual const char* mech_name(int32_t mech) {  // 0x00484650
    unported("0x00484650", {static_cast<uint32_t>(mech)});
    return "notmech";
  }
  // A mech's controls set (0x00491120: +0x37c, then the controls told their owner).
  virtual void mech_set_controls(Handle mech, Handle ctl) { unported("0x00491120", {mech, ctl}); }
  // A vehicle's centroid (0x005a85e0), and the CRT's rand.
  virtual void vehicle_position(Handle v, float out[3]) {
    const Handle p = unported("0x005a85e0", {v});
    for (int k = 0; k < 3; ++k) out[k] = zgame::from_bits(field32(p, 4 * k));
  }
  virtual int32_t crt_rand() { return static_cast<int32_t>(unported("rand", {})); }
  // zError's report, the message formatted.
  virtual void report(int32_t level, int32_t line, const char* text) {
    unported("0x0049bfd0", {level, "C:\\zipper\\Mech3\\player.cpp", line, text});
  }

  // The player's tick (0x004a1160). Silent: the frame time [0x0072c52c], the
  // pilot's vehicle (+0x14), the view [0x005f4c8c] and what it looks at
  // (+0x64), the stealth entity [0x00609a2c], [0x006099c0] cleared.
  virtual float frame_dt() = 0;
  virtual Handle pilot_vehicle() = 0;
  virtual Handle view() = 0;
  virtual Handle view_target(Handle view) = 0;
  virtual Handle stealth() = 0;
  virtual void clear_6099c0() = 0;
  virtual void vehicle_update(Handle v, float dt) { unported("pveh.vt+0x34", {v, zgame::bits(dt)}); }
  virtual void stealth_update(Handle s, float dt) { unported("stealth.vt+0x34", {s, zgame::bits(dt)}); }
  // 0x004a1140: [0x005e7284] (always 1) the entity manager's frame 0x005a8850:
  // every entity but the sensors' player (+0x09 clear) its vt+0x34.
  virtual void entities_update(float dt) { unported("0x004a1140", {zgame::bits(dt)}); }
  virtual void tick_41e3f0(float dt) { unported("0x0041e3f0", {zgame::bits(dt)}); }
  virtual void tick_537f20(int32_t v) { unported("0x00537f20", {v}); }
  virtual void vehicle_41aba0(Handle v, float dt) { unported("0x0041aba0", {v, zgame::bits(dt)}); }
  virtual void view_tick(Handle view, float dt) { unported("view.vt+0x8", {view, zgame::bits(dt)}); }

  // The handle a record goes by in the calls above (its address in the exe).
  virtual Handle rec_handle(const ai::AivRecord* rec);
  static uint32_t rec_yaw(const ai::AivRecord* rec);
  // The heading as the float its bits are (0x004a3630's argument, the
  // dword pushed as it is; a conversion of the integer made it a huge angle).
  static float rec_heading(const ai::AivRecord* rec);
};

// player.cpp's globals.
struct PlayerState {
  Handle tick = 0;                 // 0x00609a00  the object whose action is the player's tick
  float gravity = 60.0f;           // 0x00609a14  player.zrd nom_gravity
  float water_gravity = 20.0f;     // 0x00609a0c  wat_gravity, else a third of it
  float quicksand_gravity = 10.0f; // 0x00609a10  qsd_gravity, else a sixth
  uint8_t x60b56c = 0;             // 0x0060b56c  cleared with every vehicle's team masks
  float dt = 0.1f;                 // 0x005e8110  the tick's frame time
  float inv_dt = 0;                // 0x005e8114  1 / dt, 10000 at dt 0
  int32_t x5e8118 = 10;            // 0x005e8118  the argument of 0x00537f20
};
PlayerState& player_state();

void set_player_hooks(PlayerHooks* h);
PlayerHooks& player_hooks();

constexpr uint32_t kPlayerTick = 0x004a1160;  // the tick's address, as the action registers it

void player_setup(bool full);   // 0x0049e810 (CL: the app's +0x1b2, 0 after the debug Mech Lab)
bool create_vehicle(Handle def, ai::AivRecord* rec, const char* type, const char* name, const char* suffix);  // 0x0049fce0
Handle create_ai_mech(const char* type, const char* name, int32_t mech, ai::AivRecord* rec, Handle net);       // 0x0049fa20
int32_t create_controlled_vehicle(Handle def, const char* type, const char* name, ai::AivRecord* rec);        // 0x0049f800
int32_t create_basic_vehicle(Handle def, ai::AivRecord* rec, const char* type, const char* name, Handle ctl,
                             Handle* out);                                                                   // 0x0049f8a0
// The campaign's lancemates' mechs from their slots, on the player's own
// net, each with its slot's loadout and the player's paint; an empty slot's
// record leaves its triggers to the player. 1 when it ran.
bool launch_lancemates();                                      // 0x0049e5e0
void assume_duties(const ai::AivRecord* rec, const char* name);  // 0x0049e540 (ECX the pilot)
// Instant Action's line-up: each slot after the player's an AI mech from an
// unused aiv.zrd record on its side, of the nearest weight class (picked at
// random at the launch, else the farthest from the player); in wave mode
// the enemies first replaced by the wave's. `launch` 0: the next wave, its
// enemies only (the win, 0x0048f740). 1 when it ran.
bool action_lineup(bool launch);                               // 0x0049dc80
// 0x005e33a8: Instant Action's 20 waves of six (mech, loadout index), -1 for
// none; the line-up and the Action screen read it. Null past the table.
struct ActionWaveSlot {
  int32_t mech, config;
};
const ActionWaveSlot* action_wave(int32_t wave);
Handle make_mech_controls(Handle net, ai::AivRecord* rec);     // 0x0049fe90
Handle make_vehicle_controls(Handle net, ai::AivRecord* rec);  // 0x0049ff10
void set_team_masks(Handle vehicle, uint32_t team);            // inline at 0x0049f924, 0x0049fbe1, 0x00491262
void player_tick();
uint32_t player_tick_count();  // ours, for the tests: player_tick's runs                                            // 0x004a1160, the tick object's action
// [0x0060b56c] = 0: every team mask sets it so, the AiTarget constructor too
// (for mech_build and the launch, whose Handle type differs from this file's).
void player_clear_60b56c();
void player_set_60b56c(uint8_t v);
uint8_t player_get_60b56c();

}  // namespace mech3

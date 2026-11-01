#pragma once
#include <cstdint>
#include <functional>
#include <cstdio>
#include <list>
#include <vector>

// A mech's loadouts (docs/mechdef-configs.md). Original files unknown: the
// Config and its list sit between mech_anim.cpp and mech_data.cpp, the item
// and Config base methods between mech_reader.cpp and mech_salvage.cpp.
namespace mech3 {

// Items as the vtables have them; vt+0x24 (same_as) only for equipment.
struct ConfigItem {                // the shared head of every item
  int32_t count = 0;               // +0x04
  int32_t group = 0;               // +0x08
  int32_t part = -1;               // +0x0c  a part id, or a firepoint index for weapons
  uint8_t b10 = 0;                 // +0x10
  virtual ~ConfigItem() = default;
  virtual double weight(int32_t n) const = 0; // vt+0x00, n = -1 for the item's count; x87 st0
  void set_part(int32_t p) { part = p; }       // vt+0x04, 0x00409f50
  virtual int32_t location(int32_t mech) const { (void)mech; return part; }  // vt+0x08, 0x00409f40
  virtual int kind() const = 0;                // vt+0x0c
  virtual void set_id(int32_t id) = 0;         // vt+0x10
  virtual int32_t id() const = 0;              // vt+0x14
  virtual int32_t slots() const = 0;           // vt+0x18, critical slots it needs
  virtual uint32_t save(std::FILE* f) const;   // vt+0x28, 0x00472a80: kind, count, id, part, group
  virtual void load(std::FILE* f);             // vt+0x2c, 0x00472b20
};

struct BaseItem : ConfigItem {     // vtable 0x005bc160, 0x18 bytes; kind 0, never in the dfn files
  int32_t value = 0;               // +0x14
  double weight(int32_t) const override { return 0.0; }  // 0x004730d0
  int kind() const override { return 0; }
  void set_id(int32_t v) override { value = v; }
  int32_t id() const override { return value; }
  int32_t slots() const override { return 0; }
};

struct WeaponItem : ConfigItem {   // vtable 0x005c0fc0, 0x1c bytes
  int32_t weapon = -1;             // +0x14  mech-lab weapon index
  int32_t tech = 0;                // +0x18
  double weight(int32_t n) const override;  // 0x004730e0
  int kind() const override { return 1; }
  void set_id(int32_t v) override { weapon = v; }
  int32_t id() const override { return weapon; }
  int32_t slots() const override;          // 0x004731b0
  int32_t location(int32_t mech) const override;  // 0x00473220: its firepoint's part
  uint32_t save(std::FILE* f) const override;  // 0x00472bc0
  void load(std::FILE* f) override;        // 0x00472b90
};

struct AmmoItem : ConfigItem {     // vtable 0x005bd150, 0x18 bytes
  int32_t weapon = -1;             // +0x14
  double weight(int32_t n) const override;  // 0x00473140
  int kind() const override { return 2; }
  void set_id(int32_t v) override { weapon = v; }
  int32_t id() const override { return weapon; }
  int32_t slots() const override { return 1; }  // 0x004a5270
};

struct EquipmentItem : ConfigItem {  // vtable 0x005bd180, 0x20 bytes
  int32_t equipment = 0;           // +0x18  equip_kind
  int32_t tech = 0;                // +0x1c
  double weight(int32_t n) const override;  // 0x00473110
  int kind() const override { return 3; }
  void set_id(int32_t v) override { equipment = v; }  // 0x0041ec40
  int32_t id() const override { return equipment; }   // 0x0040a890
  int32_t slots() const override;          // 0x004731c0
  uint32_t save(std::FILE* f) const override;  // 0x00472c40
  void load(std::FILE* f) override;        // 0x00472c00
  bool same_as(const ConfigItem& other) const;  // vt+0x24, 0x00473480
};

struct EngineItem : ConfigItem {   // vtable 0x005c3700, 0x1c bytes; kind 4, never in the dfn files
  int32_t engine = 0;              // +0x14  engine table index
  uint8_t xl = 0;                  // +0x18
  double weight(int32_t n) const override;  // 0x00473160
  int kind() const override { return 4; }
  void set_id(int32_t v) override { engine = v; }
  int32_t id() const override { return engine; }
  int32_t slots() const override { return 0; }  // 0x004d72a0
  uint32_t save(std::FILE* f) const override;  // 0x00472cb0
  void load(std::FILE* f) override;        // 0x00472c80
};

struct Config {                    // 0xd8 bytes, vtable 0x005bd3c0
  std::list<ConfigItem*> items;    // +0x08, count +0x0c
  uint8_t base_flag = 0;           // +0x10
  int32_t mech = 0;                // +0x14
  char* name = nullptr;            // +0x18
  uint8_t player_made = 0;         // +0x1c
  uint8_t b1d = 0;                 // +0x1d
  uint8_t training = 0;            // +0x1e
  uint8_t modified = 0;            // +0x1f  not set by the ctor
  uint8_t endo_steel = 0;          // +0x20
  int32_t engine = 0;              // +0x24  index into the engine table
  uint8_t xl = 0;                  // +0x28
  float armor_tons = 0;            // +0x2c
  int32_t armor_tech = 0;          // +0x30
  uint8_t ferro = 0;               // +0x34
  int32_t armor[11] = {};          // +0x38..+0x60
  int32_t sink_tech = 0;           // +0x64
  int32_t sinks = 10;              // +0x68
  uint8_t double_sinks = 0;        // +0x6c
  uint32_t g70 = 0, g78 = 0;       // +0x70, +0x78  copies of 0x0063d870 / 0x0063d878 (0 at load)
  float turn_rate;                 // +0x74
  float accel_rate;                // +0x7c
  float decel_rate;                // +0x80
  float turn_accel;                // +0x84
  uint32_t crc = 0;                // +0x88  CRC of the packed form, set by load and save
  int32_t free_slots = 0;          // +0x8c  the critical slots left, all parts (recount)
  int32_t free_in[9] = {};         // +0x90  by part; part 0 never set (heap garbage in the exe)
  Config(int32_t mech, const char* name);                       // 0x00423d00
  ~Config();                                                    // 0x00424350
  Config(const Config&) = delete;
  Config& operator=(const Config&) = delete;
  ConfigItem* add_item(ConfigItem* item, int32_t n);            // 0x004723c0  vt+0x00
  // vt+0x14 0x00471de0: n fewer of the item (itself, else one like it; at
  // least none); armour sets the tons (half a ton each) and kind, sinks the
  // count; true only when it is gone from the list (and deleted).
  bool remove(ConfigItem* item, int32_t n);
  ConfigItem* find_like(const ConfigItem* item);                // 0x004714c0: by vt+0x24
  EquipmentItem* add_equipment(int32_t kind, int32_t tech, int32_t n);  // 0x00471fb0  vt+0x0c
  AmmoItem* add_ammo(int32_t weapon, int32_t n);                // 0x00472210  vt+0x08
  WeaponItem* add_weapon(int32_t weapon, int32_t tech);         // 0x00472330  vt+0x10
  // 0x004259c0: another player's loadout from its packed form (`words` long).
  static Config* unpacked(const uint32_t* p, size_t words);
  int32_t sink_items() const;                                   // 0x00425f30
  int32_t weapon_count() const;                                 // 0x00425ea0: weapons and TAG
  int32_t equipment_count(int32_t kind, int32_t tech) const;    // 0x00473570: tech 3 any
  // 0x004247a0 (on the armour at +0x2c, the mech pushed): the sections
  // spread anew to the points the tons give, by their shares.
  void spread_armor(int32_t mech);
  int32_t sink_limit() const { return 10; }                     // 0x00426070
  int32_t free_slots_in(int32_t part) const { return part == -1 ? free_slots : free_in[part]; }  // 0x00425e20
  int32_t slots_left() const;                                   // 0x00425d80
  int32_t ferro_slots() const;                                  // 0x00425e40
  int32_t endo_slots() const;                                   // 0x00425e80
  int32_t engine_slots() const;                                 // 0x00425e60
  void set_engine(int32_t rating);                              // 0x00432ea0
  int32_t item_count() const { return static_cast<int32_t>(items.size()); }
  // 0x00424010 with no store (ECX the name, EDX the loadout): a copy (0x00423e00)
  // under `name`, not modified, its slots counted.
  static Config* copy_of(const char* name, const Config& c);
  // The same with a step before the recount (0x00424010's store work).
  static Config* copy_of(const char* name, const Config& c, const std::function<void(Config&)>& before_recount);
  void recount();                          // 0x00425bd0: free_slots and free_in

  // The saved-loadout side (docs/mechdef-head.md).
  bool load(std::FILE* f);                 // 0x00424600, false when invalid
  uint32_t save(std::FILE* f);             // 0x00424480, sets the checksum; returns a short count
  uint32_t save_items(std::FILE* f) const; // 0x004717a0
  void load_items(std::FILE* f);           // 0x00471540
  std::vector<uint32_t> packed() const;    // 0x00425650
  uint32_t checksum() const;               // 0x004255b0
  bool validate(bool check_crc) const;     // 0x004254d0
  double weight(int32_t what) const;       // 0x00425300, -1 for the whole mech; x87 st0
  bool unplaced_item() const;              // 0x00424220
  int32_t jump_jets() const;               // 0x00425ff0
  int32_t jump_jet_limit() const;          // 0x00425fb0
  static Config* from_file(const char* name, std::FILE* f);  // 0x004242a0
};

// vt+0x24 of the item kinds: 0x00409f60 (base, ammo, engine), 0x0044ece0
// (weapons: the tech too), 0x00473480 (equipment). `in` is the listed item.
bool same_item(const ConfigItem* in, const ConfigItem* probe);
// 0x00471460 on the item list: the item itself, else one like it (0x004714c0).
ConfigItem* find_item(const std::list<ConfigItem*>& items, const ConfigItem* item);

// The item list of the loadouts' and the stores' shared base (0x00470ce0):
// 0x004717a0 writes it (none when `base_flag`), 0x00471540 reads it.
uint32_t save_item_list(const std::list<ConfigItem*>& items, uint8_t base_flag, std::FILE* f);
void load_item_list(std::list<ConfigItem*>& items, std::FILE* f);

struct ConfigList {                // 0x18 bytes
  int32_t count;                   // +0x00
  int32_t mech;                    // +0x04
  std::vector<Config*> configs;    // +0x0c
  ConfigList(int32_t mech, int32_t count);  // 0x00424a90
  ~ConfigList();                            // 0x00425110
  bool load_saved();                        // 0x00424b70, configs\<mech>.mw3
  bool save_all();                          // 0x00424f60, the player's own (+0x1c) only
  Config* find(const char* name) const;     // 0x00424e90: by name (strcmp), null when none
  bool remove(const Config* c);             // 0x00425090: the player's own only (not deleted)
};

// FIX (A) for the mech lab's Ctrl+X / Ctrl+I (0x00426180, 0x00426300; no
// Windows file dialogs): export\\<mech>_<loadout>.m3c in the write dir, and
// the next import\\*.m3c (renamed *.m3c.done once read); null when none.
bool lab_export_config(const Config& c);
Config* lab_import_config();

}  // namespace mech3

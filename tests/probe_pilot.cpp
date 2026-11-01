// Probe for test_pilot.py (docs/pilot.md): the pilot module and its games run
// a plan of creations, loads, renames, deletions, selections, saves and field
// changes against a folder standing for the install's (the write dir); the
// calls out recorded, then every pilot dumped as text.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zfix.h"
#include "mech3/mech_config.h"
#include "mech3/pilot.h"

namespace {
using namespace mech3;

std::string unhex(const std::string& h) {
  if (h == "-") return "";
  std::string s;
  for (size_t i = 0; i + 1 < h.size(); i += 2) s += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
  return s;
}

struct Log {
  std::string out;
  void add(const std::string& s) { out += s + "\n"; }
};
Log g_log;

// test_pilot.py's stand-in configuration: its key as its mech, a weapon and its ammo.
Config* fake_config(int32_t k) {
  auto* c = new Config(-1, "temp");
  c->mech = k;
  auto* w = new WeaponItem;
  w->count = 1, w->weapon = k % 4, w->tech = 1;
  auto* a = new AmmoItem;
  a->count = 10 + (k & 0xff), a->weapon = k % 4;
  c->items.push_back(w);
  c->items.push_back(a);
  return c;
}

// The original's stubs (test_pilot.py) answer the same.
struct ProbeHooks : PilotHooks {
  void store_fill(Store&, int32_t game) override { g_log.add("store_fill " + std::to_string(game)); }
  int32_t store_add_mech(Store&, int32_t mech, const char* config) override {
    g_log.add("add_mech " + std::to_string(mech) + " " + config);
    return 1;
  }
  Config* config_load(const char*, std::FILE* f) override {
    int32_t k = 0;
    const size_t n = std::fread(&k, 1, 4, f);
    g_log.add("config_load " + std::to_string(n == 4 ? k : -1));
    if (n != 4) return nullptr;
    return fake_config(k);
  }
  void config_save(Config* c, std::FILE* f) override {
    g_log.add("config_save " + std::to_string(c->mech));
    std::fwrite(&c->mech, 1, 4, f);
  }
  const char* message(int32_t id) override {
    g_log.add("message " + std::to_string(id));
    text_ = "MSG_" + std::to_string(id);
    return text_.c_str();
  }
  std::string format_ints(int32_t id, std::initializer_list<int32_t> args) override {
    std::string s = "FMT_" + std::to_string(id);
    for (int32_t a : args) s += "_" + std::to_string(a);
    g_log.add("format " + s);
    return s;
  }
  int32_t campaign_missions(int32_t c) override {
    g_log.add("missions " + std::to_string(c));
    return c + 3;
  }
  Config* config_copy(const Config& c) override {
    g_log.add("config_copy " + std::to_string(c.mech));
    return fake_config(c.mech);
  }
  void config_recount(Config& c) override { g_log.add("recount " + std::to_string(c.mech)); }
  Store* mission_salvage() override { return &salvage; }
  void save_world(const char* path, int32_t kind) override {
    g_log.add(std::string("save_world ") + path + " " + std::to_string(kind));
  }
  int32_t message_box(int32_t type, int32_t caption, int32_t text) override {
    g_log.add("ask " + std::to_string(type) + " " + std::to_string(caption) + " " + std::to_string(text) + " " +
              std::to_string(answer));
    return answer;
  }
  std::string text_;
  Store salvage;
  int32_t answer = 1;
};
ProbeHooks g_hooks;

struct Dump {
  std::ostringstream o;
  void kv(const char* k, int64_t v) { o << k << '=' << v << ' '; }
  void item(const ConfigItem* i) {
    o << "[k" << i->kind() << " n" << i->count << " g" << i->group << " p" << i->part << " b" << int(i->b10)
      << " id" << i->id();
    if (i->kind() == 1) o << " t" << static_cast<const WeaponItem*>(i)->tech;
    if (i->kind() == 3) o << " t" << static_cast<const EquipmentItem*>(i)->tech;
    if (i->kind() == 4) o << " x" << int(static_cast<const EngineItem*>(i)->xl);
    o << "] ";
  }
  void store(const char* k, const Store& s) {
    o << k << "{b" << int(s.b04) << " f" << int(s.full) << " n" << s.count() << " ";
    for (const ConfigItem* i : s.items) item(i);
    o << "} ";
  }
  void slot(const char* k, const GameSlot& s) {
    o << k << "(" << s.mech << "," << s.x0c << "," << s.skin << "," << s.x14 << ","
      << (s.config ? s.config->mech : -999) << ") ";
  }
  void game(const Game& g) {
    store("store", g.store);
    slot("slot", g.slot);
  }
};

void dump_pilot(Dump& d, Pilot* p) {
  d.o << "pilot \"" << p->name << "\" ";
  d.kv("magic", p->magic);
  d.kv("version", p->version);
  d.kv("x0c", p->x0c);
  d.kv("open", p->file != nullptr);
  d.kv("dirty", p->dirty);
  d.kv("b59", p->b59);
  d.kv("mech", p->mech);
  d.kv("x80", p->x80);
  int cur = -100;
  if (p->current == p->lab) cur = -1;
  if (p->current == p->action_game) cur = -2;
  if (p->current == p->multi) cur = -3;
  if (p->current == p->training) cur = -4;
  for (size_t i = 0; i < p->campaigns.size(); ++i)
    if (p->current == p->campaigns[i]) cur = static_cast<int>(i);
  if (!p->current) cur = -99;
  d.kv("cur", cur);
  d.kv("ncamp", p->campaign_count());
  d.o << "\n lab ";
  d.game(*p->lab);
  d.kv("level", p->lab->level);
  for (const GameSlot& s : p->lab->lineup) d.slot("l", s);
  ActionGame* a = p->action_game;
  d.o << "\n action ";
  d.game(*a);
  const int32_t st[] = {a->player_kills, a->lancemate_kills, a->enemy_kills, a->attack_wins,
                        a->attack_losses, a->defend_wins, a->defend_losses, a->waves};
  for (int32_t v : st) d.o << v << ' ';
  d.kv("x6c", a->x6c);
  d.kv("x74", a->x74);
  d.kv("x78", a->x78);
  d.kv("x7c", a->count);
  int32_t x170 = 0, x174 = 0;  // the side bytes, as the dump reads them
  int16_t x178 = 0;
  std::memcpy(&x170, a->side, 4), std::memcpy(&x174, a->side + 4, 4), std::memcpy(&x178, a->side + 8, 2);
  d.kv("x170", x170);
  d.kv("x174", x174);
  d.kv("x178", x178);
  for (const GameSlot& s : a->lineup) d.slot("l", s);
  MultiplayerGame* m = p->multi;
  d.o << "\n multi ";
  d.game(*m);
  uint32_t f74;
  std::memcpy(&f74, &m->x74, 4);
  d.kv("x70", m->x70);
  d.kv("x74", f74);
  d.kv("x78", m->x78);
  int32_t f7c;  // its bits, signed as the field was
  std::memcpy(&f7c, &m->clock_next, 4);
  d.kv("x7c", f7c);
  d.kv("x84", m->end_state);
  d.kv("b8c", m->b8c);
  d.kv("b8d", m->b8d);
  d.kv("b8e", m->b8e);
  d.kv("b8f", m->b8f);
  d.kv("x90", m->x90);
  d.kv("x94", m->x94);
  d.kv("kills", m->kills);
  d.kv("deaths", m->deaths);
  d.kv("ba8", m->ba8);
  TrainingGame* t = p->training;
  d.o << "\n training ";
  d.game(*t);
  d.kv("x38", t->x38);
  for (uint8_t b : t->passed) d.o << int(b) << ' ';
  d.kv("mission", t->mission);
  for (size_t i = 0; i < p->campaigns.size(); ++i) {
    auto* c = static_cast<CampaignGame*>(p->campaigns[i]);
    d.o << "\n campaign" << i << ' ';
    d.game(*c);
    d.kv("b38", c->b38);
    d.kv("x9c", c->x9c);
    d.kv("mission", c->mission);
    d.kv("campaign", c->campaign);
    d.kv("a8", c->a8);
    d.kv("ac", c->ac);
    d.kv("b0", c->b0);
    for (int32_t v : c->b4) d.o << v << ' ';
    d.kv("c4", c->c4);
    d.store("s3c", c->store3c);
    d.store("s50", c->store50);
    d.store("s64", c->store64);
    d.store("s78", c->store78);
    d.o << "mates ";
    for (GameSlot* s : c->lancemates) d.slot("m", *s);
  }
  d.o << '\n';
}

void dump_all(Dump& d) {
  d.o << "pilots " << pilots().size() << " current \"" << (current_pilot() ? current_pilot()->name : "-") << "\"\n";
  for (Pilot* p : pilots()) dump_pilot(d, p);
  const ActionSetup& s = action_setup();
  d.o << "setup ";
  for (int32_t v : s.g820) d.o << v << ' ';
  for (int i = 0; i < 6; ++i) d.o << s.g838[i] << ' ' << s.g858[i] << ' ';
  d.o << s.g874 << ' ' << s.g878 << ' ' << s.g87c << '\n';
}

Game* game_of(Pilot* p, int g, int i) {
  switch (g) {
    case 0: return p->lab;
    case 1: return i >= 0 && i < p->campaign_count() ? p->campaigns[static_cast<size_t>(i)] : nullptr;
    case 2: return p->action_game;
    case 3: return p->multi;
    case 4: return p->training;
    default: return nullptr;
  }
}

Store* store_of(Game* g, uint32_t off) {
  if (!g) return nullptr;
  if (off == 0x08) return &g->store;
  auto* c = dynamic_cast<CampaignGame*>(g);
  if (!c) return nullptr;
  switch (off) {
    case 0x3c: return &c->store3c;
    case 0x50: return &c->store50;
    case 0x64: return &c->store64;
    case 0x78: return &c->store78;
    default: return nullptr;
  }
}

// A field of a game by its offset in the original's object (test_pilot.py FIELDS).
void set_field(Game* g, uint32_t off, uint32_t v) {
  const auto i = static_cast<int32_t>(v);
  const auto b = static_cast<uint8_t>(v);
  if (off == 0x24) g->slot.mech = i;
  if (off == 0x30) g->slot.skin = i;
  if (off == 0x34) g->slot.x14 = i;
  if (off == 0x2c) g->slot.x0c = i;
  if (auto* c = dynamic_cast<CampaignGame*>(g)) {
    switch (off) {
      case 0x38: c->b38 = b; break;
      case 0x9c: c->x9c = i; break;
      case 0xa0: c->mission = i; break;
      case 0xa4: c->campaign = i; break;
      case 0xa8: c->a8 = i; break;
      case 0xac: c->ac = i; break;
      case 0xb0: c->b0 = b; break;
      case 0xb4: case 0xb8: case 0xbc: case 0xc0: c->b4[(off - 0xb4) / 4] = i; break;
      case 0xc4: c->c4 = i; break;
      default: break;
    }
  } else if (auto* a = dynamic_cast<ActionGame*>(g)) {
    int32_t* st[] = {&a->player_kills, &a->lancemate_kills, &a->enemy_kills, &a->attack_wins,
                     &a->attack_losses, &a->defend_wins, &a->defend_losses, &a->waves};
    if (off >= 0x4c && off <= 0x68) *st[(off - 0x4c) / 4] = i;
    if (off == 0x6c) a->x6c = i;
    if (off == 0x74) a->x74 = i;
    if (off == 0x78) a->x78 = i;
    if (off == 0x7c) a->count = i;
    if (off == 0x170) std::memcpy(a->side, &i, 4);
    if (off == 0x174) std::memcpy(a->side + 4, &i, 4);
  } else if (auto* m = dynamic_cast<MultiplayerGame*>(g)) {
    switch (off) {
      case 0x70: m->x70 = i; break;
      case 0x74: std::memcpy(&m->x74, &v, 4); break;
      case 0x78: m->x78 = i; break;
      case 0x7c: std::memcpy(&m->clock_next, &v, 4); break;
      case 0x84: m->end_state = i; break;
      case 0x8c: m->b8c = b; break;
      case 0x8d: m->b8d = b; break;
      case 0x8e: m->b8e = b; break;
      case 0x8f: m->b8f = b; break;
      case 0x90: m->x90 = i; break;
      case 0x94: m->x94 = i; break;
      case 0x98: m->kills = i; break;
      case 0x9c: m->deaths = i; break;
      case 0xa8: m->ba8 = b; break;
      default: break;
    }
  } else if (auto* t = dynamic_cast<TrainingGame*>(g)) {
    if (off == 0x38) t->x38 = i;
    if (off >= 0x3c && off <= 0x3f) t->passed[off - 0x3c] = b;
    if (off == 0x40) t->mission = i;
  } else if (auto* l = dynamic_cast<LabGame*>(g)) {
    if (off == 0x38) l->level = i;
  }
}

ConfigItem* make_item(int kind, int32_t id, int32_t extra) {
  switch (kind) {
    case 0: {
      auto* x = new BaseItem;
      x->value = id;
      return x;
    }
    case 1: {
      auto* x = new WeaponItem;
      x->weapon = id;
      x->tech = extra;
      return x;
    }
    case 2: {
      auto* x = new AmmoItem;
      x->weapon = id;
      return x;
    }
    case 3: {
      auto* x = new EquipmentItem;
      x->equipment = id;
      x->tech = extra;
      return x;
    }
    default: {
      auto* x = new EngineItem;
      x->engine = id;
      x->xl = static_cast<uint8_t>(extra);
      return x;
    }
  }
}

Pilot* named(const std::string& hexname) { return find_pilot(unhex(hexname).c_str()); }

}  // namespace

extern "C" {

// Runs `plan` (test_pilot.py's lines) with `dir` as the write dir and the
// only mount; writes the call log and the dumps into `out`. Returns the
// length, or -1.
int probe_pilot_run(const char* dir, const char* plan, char* out, int size) {
  zutl::mounts().read = {dir};
  zutl::mounts().write = dir;
  set_pilot_hooks(&g_hooks);
  g_log.out.clear();
  g_hooks.salvage.clear();
  current_pilot() = nullptr;
  action_setup() = ActionSetup{};
  Dump d;
  std::istringstream in(plan);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string op;
    ls >> op;
    std::string a, b;
    if (op == "C") {
      ls >> a;
      Pilot* p = create_pilot(unhex(a).c_str());
      g_log.add(std::string("created ") + (p ? p->name : "-"));
    } else if (op == "L") {
      load_pilots();
    } else if (op == "D") {
      ls >> a;
      Pilot* p = named(a);
      if (p && p == current_pilot()) current_pilot() = nullptr;
      delete_pilot(unhex(a).c_str());
    } else if (op == "R") {
      ls >> a >> b;
      if (Pilot* p = named(a)) p->rename(unhex(b).c_str());
    } else if (op == "S") {
      int t = 0;
      ls >> a >> t;
      if (Pilot* p = named(a)) p->select_game(t);
    } else if (op == "I") {
      int i = 0;
      ls >> a >> i;
      if (Pilot* p = named(a)) g_log.add("select_campaign " + std::to_string(p->select_campaign(i)));
    } else if (op == "X") {
      ls >> a;
      current_pilot() = a == "-" ? nullptr : named(a);
    } else if (op == "O") {
      delete_other_pilots();
    } else if (op == "Z") {
      current_pilot() = nullptr;
      delete_other_pilots();
    } else if (op == "W") {
      ls >> a;
      if (Pilot* p = named(a)) g_log.add("saved " + std::to_string(p->save()));
    } else if (op == "G") {
      int c = 0, m = 0;
      ls >> c >> m;
      Pilot* p = debug_start_campaign(c, m);
      g_log.add(std::string("debug ") + (p ? p->name : "-"));
    } else if (op == "N") {
      int c = 0, m = 0;
      ls >> a >> c >> m;
      if (Pilot* p = named(a)) p->campaigns.push_back(new CampaignGame(p, c, m));
    } else if (op == "V") {
      int g = 0, i = 0;
      uint32_t off = 0, v = 0;
      ls >> a >> g >> i >> std::hex >> off >> v >> std::dec;
      if (Pilot* p = named(a))
        if (Game* x = game_of(p, g, i)) set_field(x, off, v);
    } else if (op == "Y") {  // a setup global by its address
      uint32_t addr = 0, v = 0;
      ls >> std::hex >> addr >> v >> std::dec;
      ActionSetup& s = action_setup();
      const auto i = static_cast<int32_t>(v);
      if (addr >= 0x605820 && addr < 0x605834) s.g820[(addr - 0x605820) / 4] = i;
      if (addr >= 0x605838 && addr < 0x605850) s.g838[(addr - 0x605838) / 4] = i;
      if (addr >= 0x605858 && addr < 0x605870) s.g858[(addr - 0x605858) / 4] = i;
      if (addr == 0x605874) s.g874 = i;
      if (addr == 0x605878) s.g878 = i;
      if (addr == 0x60587c) s.g87c = i;
    } else if (op == "A" || op == "M" || op == "Q" || op == "H" || op == "J") {
      int g = 0, i = 0;
      uint32_t off = 0;
      ls >> a >> g >> i >> std::hex >> off >> std::dec;
      Pilot* p = named(a);
      Store* s = p ? store_of(game_of(p, g, i), off) : nullptr;
      if (!s) continue;
      if (op == "A") {
        int kind = 0, n = 0, id = 0, extra = 0, group = 0, part = 0;
        ls >> kind >> n >> id >> extra >> group >> part;
        ConfigItem* probe = make_item(kind, id, extra);
        probe->count = n, probe->group = group, probe->part = part;
        s->add_copy(probe, n);
        delete probe;
      } else if (op == "M") {
        int w = 0, n = 0;
        ls >> w >> n;
        s->add_ammo(w, n);
      } else if (op == "Q") {
        int kind = 0, id = 0, extra = 0, n = 0;
        ls >> kind >> id >> extra >> n;
        ConfigItem* probe = make_item(kind, id, extra);
        g_log.add("removed " + std::to_string(s->remove(probe, n)));
        delete probe;
      } else if (op == "J") {  // the first item made the store's own (as the store fill makes them)
        if (!s->items.empty()) s->items.front()->b10 = 1;
      } else {
        int mech = 0;
        ls >> mech;
        s->remove_mech(mech);
      }
    } else if (op == "T") {
      int c = 0, mech = 0, x14 = 0, skin = 0, k = 0;
      ls >> a >> c >> mech >> x14 >> skin >> k;
      Pilot* p = named(a);
      auto* g = p ? dynamic_cast<CampaignGame*>(game_of(p, 1, c)) : nullptr;
      if (!g) continue;
      auto* s = new GameSlot(mech, nullptr, skin);
      s->x14 = x14;
      if (k) s->config = fake_config(k);
      g->lancemates.push_back(s);
    } else if (op == "h") {  // the mission start's lancemate i (0x004351c0)
      int c = 0, i = 0, mech = 0, x14 = 0;
      ls >> a >> c >> i >> mech >> x14;
      Pilot* p = named(a);
      auto* g = p ? dynamic_cast<CampaignGame*>(game_of(p, 1, c)) : nullptr;
      if (!g) continue;
      auto* s = new GameSlot(mech);
      s->x14 = x14;
      g->set_lancemate(i, s);
    } else if (op == "a") {  // Instant Action: a slot added (0x00436840)
      int mech = 0, side = 0, k = 0, paint = 0;
      ls >> a >> mech >> side >> k >> paint;
      Pilot* p = named(a);
      ActionGame* g = p ? p->action_game : nullptr;
      if (!g) continue;
      Config* c = k ? fake_config(k) : nullptr;
      g_log.add("added " + std::to_string(g->add(mech, static_cast<uint8_t>(side), c, paint) != nullptr));
      delete c;
    } else if (op == "e") {  // its enemies dropped (0x00436ac0), or slot i removed (0x00436af0)
      int i = 0;
      ls >> a >> i;
      Pilot* p = named(a);
      ActionGame* g = p ? p->action_game : nullptr;
      if (!g) continue;
      if (i < -1) g->drop_enemies();
      else g_log.add("removed " + std::to_string(g->remove(i) ? 1 : 0));
    } else if (op == "k") {  // their count (0x00435230)
      int c = 0, n = 0;
      ls >> a >> c >> n;
      Pilot* p = named(a);
      if (auto* g = p ? dynamic_cast<CampaignGame*>(game_of(p, 1, c)) : nullptr) g->set_lancemate_count(n);
    } else if (op == "K") {  // a game's own slot's configuration
      int g = 0, i = 0, k = 0;
      ls >> a >> g >> i >> k;
      Pilot* p = named(a);
      Game* x = p ? game_of(p, g, i) : nullptr;
      if (!x) continue;
      delete x->slot.config;
      x->slot.config = nullptr;
      if (k) x->slot.config = fake_config(k);
    } else if (op == "P") {  // the save after a mission, with the overwrite question's answer
      int next = 0;
      ls >> a >> next >> g_hooks.answer;
      if (Pilot* p = named(a)) g_log.add("recorded " + std::to_string(p->record_mission(next != 0)));
    } else if (op == "l") {
      int c = 0, del = 0;
      ls >> a >> c >> del;
      Pilot* p = named(a);
      if (auto* g = p ? dynamic_cast<CampaignGame*>(game_of(p, 1, c)) : nullptr) g->reset_lancemates(del != 0);
    } else if (op == "s") {  // an item in the mission's salvage
      int kind = 0, n = 0, id = 0, extra = 0, group = 0, part = 0;
      ls >> kind >> n >> id >> extra >> group >> part;
      ConfigItem* probe = make_item(kind, id, extra);
      probe->count = n, probe->group = group, probe->part = part;
      g_hooks.salvage.add_copy(probe, n);
      delete probe;
    } else if (op == "f") {  // every configuration of a campaign marked modified
      int c = 0;
      ls >> a >> c;
      Pilot* p = named(a);
      if (auto* g = p ? dynamic_cast<CampaignGame*>(game_of(p, 1, c)) : nullptr) {
        if (g->slot.config) g->slot.config->modified = 1;
        for (GameSlot* s : g->lancemates)
          if (s->config) s->config->modified = 1;
      }
    } else if (op == "E") {
      dump_all(d);
    } else if (op == "F" || op == "B" || op == "U") {  // a file made, overwritten in part or cut
      ls >> a >> b;
      const std::string host = zutl::write_path(unhex(a).c_str());
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::path(host).parent_path(), ec);
      std::string data;
      if (std::FILE* f = std::fopen(host.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        std::fclose(f);
      } else if (op != "F") {
        continue;
      }
      if (op == "F") {
        data = unhex(b);
      } else if (op == "U") {
        const size_t len = std::stoul(b, nullptr, 16);
        if (len < data.size()) data.resize(len);
      } else {
        std::string c;
        ls >> c;
        const size_t pos = std::stoul(b, nullptr, 16);
        const std::string bytes = unhex(c);
        if (data.size() < pos + bytes.size()) data.resize(pos + bytes.size(), '\0');
        data.replace(pos, bytes.size(), bytes);
      }
      if (std::FILE* f = std::fopen(host.c_str(), "r+b")) {  // in place: the name keeps its case
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        std::filesystem::resize_file(host, data.size(), ec);
      } else if (std::FILE* g = std::fopen(host.c_str(), "wb")) {
        std::fwrite(data.data(), 1, data.size(), g);
        std::fclose(g);
      }
    }
  }
  current_pilot() = nullptr;
  while (!pilots().empty()) {  // the plan ends with Z; anything left goes quietly
    Pilot* p = pilots().front();
    pilots().pop_front();
    p->dirty = 0;
    delete p;
  }
  const std::string all = g_log.out + "\n--\n" + d.o.str();
  if (static_cast<int>(all.size()) + 1 > size) return -1;
  std::memcpy(out, all.c_str(), all.size() + 1);
  return static_cast<int>(all.size());
}

// test_store_fill.py: a store with `before` mechs in it filled by 0x00470e30
// (Store::fill_all), dumped as "full count" then "kind id tech count b10
// group part" per item; `bugs` the zfix switch (the plain PPC).
int probe_store_fill(int before, int bugs, char* out, int size) {
  const bool saved = zfix::options().bugs;
  zfix::options().bugs = bugs != 0;
  mech3::Store st(0);
  for (int i = 0; i < before; ++i) st.add_mech(i % 19);
  st.fill_all();
  std::ostringstream o;
  o << int(st.full) << ' ' << st.count() << '\n';
  for (const mech3::ConfigItem* it : st.items) {
    int32_t tech = 0;
    if (auto* w = dynamic_cast<const mech3::WeaponItem*>(it)) tech = w->tech;
    if (auto* q = dynamic_cast<const mech3::EquipmentItem*>(it)) tech = q->tech;
    o << it->kind() << ' ' << it->id() << ' ' << tech << ' ' << it->count << ' ' << int(it->b10) << ' ' << it->group
      << ' ' << it->part << '\n';
  }
  zfix::options().bugs = saved;
  const std::string all = o.str();
  if (static_cast<int>(all.size()) >= size) return -1;
  std::memcpy(out, all.c_str(), all.size() + 1);
  return static_cast<int>(all.size());
}

}  // extern "C"

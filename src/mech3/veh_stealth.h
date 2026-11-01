#pragma once
#include <cstdint>

#include "gamez/ztwod/twod.h"
#include "mech3/veh_basic_object.h"
#include "mech3/veh_controls.h"

// The stealth object (docs/editor.md, The stealth object): a basic vehicle
// made at every mission load ("Creating Stealth", [0x00609a2c]) with no
// model, sounds or weapons (vehicle.zrd's `stealth`), flown with the player's
// own movement commands while the view is on it: the camera of Zipper's
// Battlesport game editor. Original file unknown: these sit after the basic
// vehicle's movement (0x004a1d90..) and before mech3\veh_basic.cpp.
namespace mech3 {

// Its controls (0x004a51b0 over 0x004ac750, 0xa1c bytes; vtables 0x005c86b0
// and IControls 0x005c85f8): the base's but for vt+0x1c (0x004d72a0: never
// destroyed), vt+0x20 (0x004a5270: 1) and the frame, IControls vt+0x0c
// (0x004a5ae0 -> 0x004a5280: the player's throttle, turn and pitch commands
// and the mouse, S3). The throttle and turn accessors are the shared ones
// (0x004a5b20..0x004a5bb0, the AI's too).
struct StealthControls : VehicleControls {
  bool destroyed() override { return false; }  // vt+0x1c 0x004d72a0
  int32_t dynamics() override { return 1; }    // vt+0x20 0x004a5270
  int update(float dt) override;               // IControls vt+0x0c 0x004a5280
  uint32_t xa18 = 0;                           // +0xa18 (0x004a51b0 zeroes it)
};

struct Stealth;

// Its overlay (0x004a55c0, 0x108 bytes, vtable 0x005c86e8): a zTwoD layer,
// active from the start, holding the image "ret_off" (0x004a5850: a plain
// image, centred at (0, 0), shown for good); its frame (0x004a56b0) writes a
// readout of the stealth's movement mode (BASIC, TRACK, HOVER, AMPHIB, SUB)
// while the view is on it (S4).
struct StealthLayer : ztwod::Layer {
  Stealth* owner = nullptr;  // +0x10
  ztwod::Image image{0};     // +0x14
  explicit StealthLayer(Stealth* s);   // 0x004a55c0
  ~StealthLayer() override = default;  // 0x004a5640
  void update(float dt) override;      // +0x00 0x004a56b0
  void set_active(int32_t on) override;  // +0x04 0x004a5690
};

// 0x104 bytes, vtable 0x005c8460 (the basic vehicle's, its destructor
// 0x004a4fc0 apart).
struct Stealth : BasicVehicle {
  StealthLayer* layer = nullptr;  // +0xfc
};

// The calls out the stealth makes beyond the basic vehicle's
// (VehBasicHooks, whose node, entity, position and markers calls it shares).
class StealthHooks : public zgame::Unported {
 public:
  using Handle = mech3::Handle;
  static uint32_t low(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  static Handle h(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }
  virtual uint32_t ref(const void* p) { return low(p); }
  virtual void on_new(uint32_t size, const void* p) { (void)size, (void)p; }  // operator new 0x104, 0x108
  virtual Handle object3d_create() { return h(unported("0x004fb740", {})); }
  virtual zrdr::Node* reader_load(const char* file) {  // 0x00556740(file, 0)
    return static_cast<zrdr::Node*>(h(unported("0x00556740", {file, 0})));
  }
  virtual zrdr::Node* reader_find(zrdr::Node* node, const char* key) {  // 0x005569e0
    return static_cast<zrdr::Node*>(h(unported("0x005569e0", {low(node), key})));
  }
  virtual zrdr::Node* reader_item(zrdr::Node* node, int32_t i);  // the node's list +0x20: item 4
  virtual void reader_free(zrdr::Node* root) { unported("0x00556820", {low(root)}); }
  // aiv.zrd's record by its name: the reader made (0x004e0490), the
  // record's position (+0x08) and heading (+0x14, its bits) when there is
  // one (0x004e0680), the reader freed (0x004e04f0).
  virtual void aiv_open() { unported("0x004e0490", {}); }
  virtual bool aiv_named(const char* name, float pos[3], float* yaw) {
    (void)pos, (void)yaw;
    return unported("0x004e0680", {name}) != 0;
  }
  virtual void aiv_close() { unported("0x004e04f0", {}); }
  // The overlay (operator new 0x108, 0x004a55c0).
  virtual StealthLayer* layer_create(Stealth& s);
  // The basic vehicle's own, by the original's address: its setup
  // (0x0049ec00), motion block (0x0049ec90), points (0x0049f660), placement
  // (0x004a3630); ported, called through here so the tests can stub them.
  virtual void vehicle_setup(BasicVehicle& v, zrdr::Node* common, const char* name, int32_t kind);
  virtual void vehicle_motion(BasicVehicle& v, zrdr::Node* mode, const char* name, const char* type);
  virtual void vehicle_points(BasicVehicle& v);
  virtual void vehicle_place(BasicVehicle& v, const float pos[3], float yaw);
  // The basic vehicle's constructor (0x004ac530) and destructor (0x004ac650).
  virtual void vehicle_construct(BasicVehicle& v, Handle ctl);
  virtual void vehicle_destroy(BasicVehicle& v);
  // The overlay's: the image's texture (0x0059bb10), the readout (S4), and
  // what turning it off calls (0x0040fb50(0), S4).
  virtual void image_set(ztwod::Image& i, const char* name) { i.set_image(name); }
  virtual void readout(StealthLayer& l) { unported("0x004a56b0", {ref(&l)}); }
  virtual void layer_off() { unported("0x0040fb50", {0}); }
  virtual int frame(StealthControls& c, float dt) { return static_cast<int>(unported("0x004a5280", {ref(&c), zgame::bits(dt)})); }
};
void set_stealth_hooks(StealthHooks* h);  // null: the defaults
StealthHooks& stealth_hooks();

// Mech3.exe 1.2 @ 0x004a4de0: the stealth object over `ctl` (its own
// controls, 0x004a51b0's), named "Stealth" on a new node in the world, at
// the origin, out of the ground queries and collisions, set up from
// vehicle.zrd's `stealth` (its common_mode and its motion block, mode
// BASIC), its overlay made, and placed where aiv.zrd's `stealth` record is
// when the mission has one. Its +0x09 (zEntity's: run by the entity walk)
// is 0.
Stealth* stealth_create(StealthControls* ctl);
// Mech3.exe 1.2 @ 0x004a4fc0 and the delete (0x004a4fa0(1)).
void stealth_delete(Stealth* s);

}  // namespace mech3

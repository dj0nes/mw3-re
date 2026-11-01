// The 1.2 data layer's messages (docs/patch12.md): Mech3Msg.dll 1.0 -> 1.2.
#include <cstdint>

#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/patch12.h"

namespace mech3::patch12 {
namespace {

struct Message {
  int32_t id;
  const char* key;  // ZLocGetID's name; null: the key is 1.0's already
  const char* text;
  bool ours;        // our wording, not 1.2's
};

// Names (keys, targets, resolutions, a file name) are the words themselves;
// the sentences are ours.
constexpr Message kMessages[] = {
    {0x51b, "MSG_KEY_ALT", "Alt", false},
    {0x51c, "MSG_KEY_SHIFT", "Shift", false},
    {0x601, "MSG_JBTN_1", "Button 1", false},
    {0x609, "MSG_JBTN_9", "Button 9", false},
    {0x60a, "MSG_JBTN_10", "Button 10", false},
    {0xfc0, "MSG_DEFAULT_CTL_CONFIG", "DefaultCtlConfig", false},  // a controls file's name
    {0xfc1, "MSG_MAX_JJETS", "The %1 has room for no more than %2!d! jump jets", true},
    {0xfc2, "MSG_TXT_ROUNDS", "rounds", false},
    {0xfc3, "MSG_DELIVERY_SOURCE1", "Delivery Source 1", false},
    {0xfc4, "MSG_DELIVERY_SOURCE2", "Delivery Source 2", false},
    {0xfc5, "MSG_DELIVERY_SOURCE3", "Delivery Source 3", false},
    {0xfc6, "MSG_DELIVERY_SOURCE4", "Delivery Source 4", false},
    {0xfc7, "MSG_DOME_GENERATOR", "Dome Generator", false},
    {0xfc8, "MSG_MAIN_QUARTERS", "Main quarters", false},
    {0xfc9, "MSG_CORRIDOR_GATEWAY", "Corridor Gateway", false},
    {0xfca, "MSG_CLAN_FORTRESS", "Clan Fortress", false},
    {0xfcb, "MSG_CLAN_SPACEPORT", "Clan Spaceport", false},
    {0xfcc, "MSG_CLAN_FACTORY", "Clan Factory", false},
    {0xfcd, "MSG_CENTRAL_BRIDGE", "Central Bridge", false},
    {0xfce, "MSG_TOWER_BRIDGE", "Tower Bridge", false},
    {0xfcf, "MSG_FACTORY_BRIDGE", "Factory Bridge", false},
    {0xfd0, "MSG_WATER_CONDUIT", "Water Conduit", false},
    {0xfd1, "MSG_MECH_FACTORY", "Mech Factory", false},
    {0xfd2, "MSG_FACTORY_DOOR", "Factory Door", false},
    {0xfd3, "MSG_ORE_CRUSHER", "Ore Crusher", false},
    {0xfd4, "MSG_HOTEL", "Hotel No_tell", false},  // 1.0's own target name (c2m3 targets.zrd)
    {0xfd5, "MSG_EXIT_VENT", "Exit Vent", false},
    {0xfd6, "MSG_AIR_REFINERY", "Air Refinery", false},
    {0xfd7, "MSG_SUPPLY_LIFT", "Supply lift", false},
    {0xfd8, "MSG_DOCKED_SHUTTLE", "Docked Shuttle", false},
    {0xfd9, "MSG_SHUTTLECRAFT", "Shuttlecraft", false},
    {0xfda, "MSG_1152x864", "1152 x 864", false},
    {0xfdb, "MSG_1280x1024", "1280 x 1024", false},
    {0xfdc, "MSG_1600x1200", "1600 x 1200", false},
    {0xfdd, "MSG_VERSION_NEW", "The host is running an older MechWarrior 3. The host needs the 1.2 patch to join you.", true},
    {0xfde, "MSG_VERSION_OLD", "This game needs a newer MechWarrior 3. Install the 1.2 patch to join it.", true},
    {0xfdf, "MSG_MPDB_UNAVAILABLE", "UNAVAILABLE", false},
    {0xfe0, "MSG_MP_NODATABASE",
     "These players do not have the chosen world:\r\n\r\n%1\r\n\r\nPick another world, or remove them from the game.", true},
    // Edited: real line breaks for 1.0's literal "\n"; the chat labels' leading
    // space; the team label takes its name as %1.
    {0x1e6, nullptr, "This team name is already taken.\r\n\r\nPick another one.", false},
    {0x899, nullptr, " Send message to team", false},
    {0x89a, nullptr, " Send message to all", false},
    {0xf64, nullptr, "%1 Team", false},
};

}  // namespace

void apply_messages(zutl::MessageDll& dll) {
  for (const Message& m : kMessages) {
    dll.text[m.id] = m.text;
    if (m.key) dll.ids[m.key] = m.id;
  }
  // 1.2 drops the version line "MechWarrior 3 %1"; nothing asks for it.
  dll.text.erase(0x4);
  dll.ids.erase("MSG_GAME_NAME_VER");
}

bool message_is_ours(int32_t id) {
  for (const Message& m : kMessages)
    if (m.id == id) return m.ours;
  return false;
}

}  // namespace mech3::patch12

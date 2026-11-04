// Probes for test_pm_sound.py: PM's zSound (src/gamez/zsound on this branch).
// The rest of the probe is MW3's harness (tests/probe_sound.cpp, built into
// pmprobe as well).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"

namespace {
std::string g_extra;
uint32_t ub(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
}  // namespace

extern "C" {

// The original's path (pmprobe has no probe.cpp to turn the fixes off).
void probe_pm_snd_use() {
  zfix::options().per_second = false;
  zfix::options().bugs = false;
}

// PM's statics after probe_snd_begin's fresh State: its defaults are PM's
// image (345 m/s, 4 duplicates), so nothing to do; kept for the test.
void probe_pm_snd_statics() {}

void probe_pm_snd_range_scale(float s) { zsnd::set_range_scale(s); }

// After probe_snd_setup on a folder with a changed reader.zbd: the install
// behind it, for the sound archives.
void probe_pm_snd_mount(const char* dir) { zutl::mount(dir); }

// What PM's State holds besides the shared dump: the range scale, the default
// queue priority, COMPILE_FORMAT.
const char* probe_pm_snd_extra() {
  const zsnd::State& st = zsnd::state();
  char buf[128];
  std::snprintf(buf, sizeof buf, "pm rs%08x qp%d", ub(st.range_scale), st.default_qpriority);
  g_extra = buf;
  for (const zsnd::Format& f : st.compile) {
    if (f.has_file) std::snprintf(buf, sizeof buf, " {%s}", f.file.c_str());
    else std::snprintf(buf, sizeof buf, " {%d %d %d}", f.rate, f.bits, f.channels);
    g_extra += buf;
  }
  g_extra += "\n";
  return g_extra.c_str();
}

// PM's CD notify (0x005938c0) after setting the CD's state: mode, track and
// last track asked for, open or not. Returns the last track asked for and
// the track (<< 8): a play that went through seeks, one cut short does not.
int probe_pm_snd_cd_notify(int open, int mode, int track, int last, int status) {
  zsnd_cd::State& cd = zsnd_cd::state();
  cd.flags = open ? 2u : 0u;
  cd.mode = mode, cd.track = track, cd.last_track = last;
  zsnd_cd::notify(status);
  return cd.last_track | cd.track << 8;
}

}  // extern "C"

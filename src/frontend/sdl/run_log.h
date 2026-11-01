#pragma once
// mw3.log (ours, not original code; docs/program.md, The log): everything mw3
// prints, on stdout and stderr, copied to <write dir>/mw3.log, made new each
// start; the last start's is kept as mw3.old.log, so a crash's log survives
// the restart a player makes before reporting it.
#include <memory>
#include <string>

namespace sdlfe {

class RunLog {
 public:
  // The log in `dir`; false (and no log) when it cannot be made. The console
  // keeps every line as before.
  RunLog();
  bool open(const std::string& dir, const std::string& name = "mw3");  // <name>.log, <name>.old.log
  ~RunLog();  // the last lines written, the console's own handles back
  const std::string& path() const { return path_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string path_;
};

}  // namespace sdlfe

#include <csignal>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#if defined(_WIN32)
#include <process.h>
#endif
#include <filesystem>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "check.h"

namespace check {
std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}
int failures = 0;
}  // namespace check

const char* mw3_test_tmp() {
  // Never freed: the exit's cleanup runs after the statics are gone.
  static const std::string* dir = new std::string([] {
    namespace fs = std::filesystem;
    const fs::path root = fs::path(MW3_TEST_TMP_ROOT) / "test-tmp";
    std::error_code ec;
#if !defined(_WIN32)
    // The folders of runs that died (no process of that id) cleared.
    for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
      const long pid = std::strtol(e.path().filename().string().c_str(), nullptr, 10);
      if (pid > 0 && kill(static_cast<pid_t>(pid), 0) != 0) fs::remove_all(e.path(), ec);
    }
    const long me = static_cast<long>(getpid());
#else
    const long me = static_cast<long>(_getpid());
#endif
    const fs::path d = root / std::to_string(me);
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d.string();
  }());
  static const bool cleanup = (std::atexit([] {
    std::error_code ec;
    std::filesystem::remove_all(*dir, ec);
  }), true);
  (void)cleanup;
  return dir->c_str();
}

#if !defined(_WIN32)
namespace {
// A crash prints where it happened and leaves at once (no crash report to wait
// on, which kept dying test processes stuck at exit).
void on_crash(int sig) {
  void* frames[48];
  const int n = backtrace(frames, 48);
  const char msg[] = "mw3tests: crashed, backtrace:\n";
  write(2, msg, sizeof msg - 1);
  backtrace_symbols_fd(frames, n, 2);
  _exit(128 + sig);
}
}  // namespace
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MW3_ASAN 1
#endif
#endif

int main() {
#if defined(_WIN32)
  // No crash dialogs: a test that crashes exits (the run is often headless, over ssh).
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#elif !defined(MW3_ASAN)  // AddressSanitizer reports crashes itself
  static char alt[1 << 16];
  stack_t ss{};
  ss.ss_sp = alt, ss.ss_size = sizeof alt;
  sigaltstack(&ss, nullptr);
  for (int sig : {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
    struct sigaction sa{};
    sa.sa_handler = on_crash;
    sa.sa_flags = SA_ONSTACK;
    sigaction(sig, &sa, nullptr);
  }
#endif
  // A test that crashes still shows where. Windows' CRT has no line buffering
  // (and takes size 0 as an invalid argument): unbuffered there.
#if defined(_WIN32)
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
  // MW3_TEST=name runs only the tests whose names contain it; a,b,c any of them.
  // MW3_SKIP=name (the same form) leaves those out: MW3_SKIP=headless skips the
  // whole-mission runs, nearly all of the suite's time.
  const char* only = std::getenv("MW3_TEST");
  const char* skip = std::getenv("MW3_SKIP");
  auto any_in = [](const char* list, const char* name) {
    for (const char* p = list; *p;) {
      const char* e = std::strchr(p, ',');
      const std::string part(p, e ? e - p : std::strlen(p));
      if (!part.empty() && std::strstr(name, part.c_str())) return true;
      if (!e) break;
      p = e + 1;
    }
    return false;
  };
  auto wanted = [&](const char* name) {
    if (skip && any_in(skip, name)) return false;
    return !only || any_in(only, name);
  };
  size_t ran = 0;
  for (const check::Test& t : check::registry()) {
    if (!wanted(t.name)) continue;
    ++ran;
    if (std::getenv("MW3_TEST_TRACE")) std::fprintf(stderr, "start %s\n", t.name);
    const int before = check::failures;
    t.fn();
    std::printf("%s %s\n", check::failures == before ? "ok  " : "FAIL", t.name);
#if !defined(_WIN32)
    // Ours: MW3_TEST_FDS=1, the open files after each test (a leak would
    // starve Windows' 512 FILEs long before a POSIX limit).
    if (std::getenv("MW3_TEST_FDS")) {
      int n = 0;
      if (DIR* d = opendir("/dev/fd")) {
        while (readdir(d)) ++n;
        closedir(d);
      }
      std::fprintf(stderr, "fds %d after %s\n", n, t.name);
#if defined(__APPLE__)
      if (std::strcmp(std::getenv("MW3_TEST_FDS"), "2") == 0)  // each one's file
        for (int fd = 3; fd < 4096; ++fd) {
          char path[1024];
          if (fcntl(fd, F_GETPATH, path) == 0) std::fprintf(stderr, "  fd %d %s\n", fd, path);
        }
#endif
    }
#endif
  }
  std::printf("%zu tests, %d failed checks\n", ran, check::failures);
  return check::failures == 0 ? 0 : 1;
}

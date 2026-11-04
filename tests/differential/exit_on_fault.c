// Ours, for break runs on the Mac: a process that crashes there can stick in
// the unkillable UE state (a test that burns CPU, then freezes). Loaded into
// a test, a fatal signal prints a backtrace and leaves by _exit (128 + N), so
// the crash is a plain failure. Use: docs/worklogs/README.md, run_capped.
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void on_fault(int sig) {
  static const char text[] = "exit_on_fault: fatal signal, backtrace:\n";
  write(2, text, sizeof text - 1);
  void* frames[32];
  backtrace_symbols_fd(frames, backtrace(frames, 32), 2);
  _exit(128 + sig);
}

__attribute__((constructor)) static void install(void) {
  stack_t alt;  // a stack overflow's handler needs a stack of its own
  alt.ss_sp = malloc(1 << 18);
  alt.ss_size = 1 << 18;
  alt.ss_flags = 0;
  sigaltstack(&alt, 0);
  struct sigaction a;
  memset(&a, 0, sizeof a);
  a.sa_handler = on_fault;
  a.sa_flags = SA_ONSTACK;
  sigaction(SIGSEGV, &a, 0);
  sigaction(SIGBUS, &a, 0);
  sigaction(SIGABRT, &a, 0);
  sigaction(SIGILL, &a, 0);
}

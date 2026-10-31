"""Runs one test script and leaves with os._exit, whatever happens.

  run_exit.py test_x.py [args...]

A test that fails with an exception leaves through the interpreter's
teardown, where Unicorn can hang the process at exit for good on this Mac
(the stuck "UE" processes); os._exit skips the teardown. The exit code is
the script's (1 for an exception).
"""
import os
import runpy
import sys
import traceback

script = sys.argv[1]
sys.argv = sys.argv[1:]
sys.path.insert(0, os.path.dirname(os.path.abspath(script)))
code = 0
try:
    runpy.run_path(script, run_name='__main__')
except SystemExit as e:
    code = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
except BaseException:
    traceback.print_exc()
    code = 1
sys.stdout.flush()
sys.stderr.flush()
os._exit(code)

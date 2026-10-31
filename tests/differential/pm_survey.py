"""Runs the differential tests on Pirate's Moon's exe (MW3_EXE=pm) and sorts
them by what PM did to the functions they name (docs/pm-map.md, step 0).

  pm_survey.py [--classify] [--secs N] [--exe mw3|pm] [test ...]

--exe mw3 runs them on MW3's exe the same way (survey-mw3.json), to check
that the PM translation left MW3's runs alone.

A test's functions are the MW3 function entries written in it (hex
constants that pmdiff.py paired or left MW3 only). "unchanged" means every
one is identical or changed only by the compiler. Each test runs once, one
at a time, under run_capped.py; results go to
analysis-output/pm-diff/survey.json.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PAIRS = ROOT / 'analysis-output' / 'pm-diff' / 'pairs.json'
OUT = ROOT / 'analysis-output' / 'pm-diff' / 'survey.json'


def statuses():
    st = {}
    for p in json.loads(PAIRS.read_text()):
        if 'mw3' not in p:
            continue
        k = p['kind']
        st[int(p['mw3'], 16)] = ('identical' if k == 'identical' else
                                 ('compiler' if not p['feature_diff'] else 'changed') if k == 'changed' else 'gone')
    return st


def classify(path, st):
    found = {}
    for v in re.findall(r'0x0*([0-9a-fA-F]{6})\b', path.read_text(errors='replace')):
        a = int(v, 16)
        if a in st:
            found[a] = st[a]
    kinds = set(found.values())
    if not found:
        return 'no functions', found
    if kinds <= {'identical', 'compiler'}:
        return 'unchanged', found
    return 'gone' if 'gone' in kinds else 'changed', found


def main():
    args = sys.argv[1:]
    secs = '900'
    if '--secs' in args:
        i = args.index('--secs')
        secs = args[i + 1]
        del args[i:i + 2]
    exe = 'pm'
    if '--exe' in args:
        i = args.index('--exe')
        exe = args[i + 1]
        del args[i:i + 2]
    out_path = OUT if exe == 'pm' else OUT.with_name(f'survey-{exe}.json')
    only_classify = '--classify' in args
    args = [a for a in args if a != '--classify']
    st = statuses()
    tests = [HERE / a for a in args] if args else sorted(HERE.glob('test_*.py'))
    results = json.loads(out_path.read_text()) if out_path.exists() and args else {}
    for t in tests:
        cls, found = classify(t, st)
        r = {'class': cls, 'functions': len(found)}
        if not only_classify:
            env = dict(os.environ, MW3_EXE=exe)
            p = subprocess.run([sys.executable, str(HERE / 'run_capped.py'), '--secs', secs, '--',
                                str(ROOT / '.venv/bin/python'), str(HERE / 'run_exit.py'), str(t)], cwd=ROOT, env=env,
                               capture_output=True, text=True)
            out = (p.stdout + p.stderr).strip().splitlines()
            capped = [l for l in out if l.startswith('run_capped:')]
            unmapped = any('Unmapped' in l for l in out)
            exit_ok = capped and 'exit 0' in capped[-1]
            r.update(passed=bool(exit_ok), unmapped=unmapped, tail=[l[:200] for l in out[-4:]])
        results[t.name] = r
        print(f"{t.name}: {r['class']} ({r['functions']})" +
              ('' if only_classify else f" {'PASS' if r['passed'] else 'FAIL'}{' unmapped' if r.get('unmapped') else ''}"),
              flush=True)
    if not only_classify:
        out_path.write_text(json.dumps(results, indent=1))


if __name__ == '__main__':
    main()

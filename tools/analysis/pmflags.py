"""The pairs pmdiff.py calls changed only by the compiler, checked for what
pmdiff does not count (docs/pm-map.md): field offsets that differ (a struct
whose layout PM changed) and small constants in comparisons and masks.
Writes analysis-output/pm-diff/compiler-flags.json.
"""
import json,os,re,sys
from collections import Counter
sys.path.insert(0,'tools/analysis'); sys.argv=['x']
import pmdiff
AO=pmdiff.AO; ROOT=pmdiff.ROOT
mw3=pmdiff.Exe('MW3',os.path.join(ROOT,'game/mw3-1.2/Mech3.exe'),os.path.join(AO,'pm-diff/mw3/mech3-bytes.asm'),os.path.join(AO,'pm-diff/mw3/functions.jsonl'))
pm=pmdiff.Exe('PM',os.path.join(ROOT,'game/pm/Mech3.exe'),os.path.join(AO,'pm.asm'),os.path.join(AO,'pm-decomp/functions.jsonl'))
pairs=json.load(open(os.path.join(AO,'pm-diff/pairs.json')))
def small(e,d):
    c=Counter(); offs=Counter()
    for a in d['body']:
        raw,mn,ops=e.ins[a]
        if mn in pmdiff.BRANCH or 'esp' in ops or 'ebp' in ops or mn in ('push','sub','add','lea','ret','enter') and 'esp' in ops: continue
        outer=re.sub(r'\[[^\]]*\]','',ops)
        for v in re.findall(r'(?<![\w])(0x[0-9a-f]+|\d+)(?![\w])',outer):
            n=int(v,16) if v.startswith('0x') else int(v)
            if n<0x100 and mn in ('cmp','test','and','or'): c[n]+=1
        for m in re.findall(r'\[([^\]]*)\]',ops):
            for v in re.findall(r'\+ ?(0x[0-9a-f]+)',m):
                if int(v,16) < 0x10000: offs[int(v,16)]+=1
    return c,offs
res=Counter(); lay=0; both=0; ex=[]; flags={}
for p in pairs:
    if p['kind']!='changed' or p['feature_diff']: continue
    da,db=mw3.f.get(int(p['mw3'],16)),pm.f.get(int(p['pm'],16))
    if not da or not db: continue
    ca,oa=small(mw3,da); cb,ob=small(pm,db)
    imm = set(ca) != set(cb)
    lo = set(oa)!=set(ob)
    flags[p['mw3']]={'layout':lo,'imm':imm,'offs_a':sorted(set(oa)-set(ob))[:8],'offs_b':sorted(set(ob)-set(oa))[:8],'imm_a':sorted(set(ca)-set(cb)),'imm_b':sorted(set(cb)-set(ca))}
    res['imm']+=imm; res['layout']+=lo; res['both']+=imm and lo; res['n']+=1
    if imm and not lo and len(ex)<15: ex.append((p['mw3'],p['pm'],sorted(set(ca)-set(cb))[:3],sorted(set(cb)-set(ca))[:3]))
print(res); 
for x in ex: print(x)

json.dump(flags,open(os.path.join(AO,'pm-diff/compiler-flags.json'),'w'))

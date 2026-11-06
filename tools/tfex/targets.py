"""Lists TF.EX's [HarmonyPatch] targets as (type, method, project) triples. Usage: targets.py <tfex src dir> <out.json>"""
import re, os, sys, json
root = sys.argv[1]; out = []
pat = re.compile(r'\[HarmonyPatch\(([^\]]*)\)\]')
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith('.cs'): continue
        src = open(os.path.join(dp, f), encoding='utf-8-sig', errors='replace').read()
        cur = None
        for m in pat.finditer(src):
            a = m.group(1)
            t = re.search(r'typeof\(([\w.]+)\)', a)
            names = re.findall(r'nameof\(([\w.]+)\)|"(\w+)"', a)
            meth = None
            for n1, n2 in names:
                meth = (n1.split('.')[-1] if n1 else n2); break
            if 'MethodType.Constructor' in a: meth = '.ctor'
            if 'MethodType.Getter' in a and meth: meth = 'get_' + meth
            if 'MethodType.Setter' in a and meth: meth = 'set_' + meth
            if t:
                ty = t.group(1).split('.')[-1]
                if meth is None: cur = ty; continue
            else:
                ty = cur
            if ty and meth: out.append((ty, meth, os.path.relpath(os.path.join(dp, f), root).split('/')[1]))
json.dump(out, open(sys.argv[2], 'w'))
print(len(out), len(set((a,b) for a,b,_ in out)), len(set(a for a,_,_ in out)))

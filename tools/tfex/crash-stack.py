#!/usr/bin/env python3
"""crash-stack.py: a Ryujinx guest stack trace, named. Our module's frames through addr2line on the ELF of the build that ran, the
game's frames by the nearest game method the binding (tfexgen_binding.hpp) and the hand code's offsets know (a method start at or
below the address: good for "which call", not proof). Plus the last [hw] lines before the crash.

  tools/tfex/crash-stack.py <ryujinx.log> [module.elf]     (default ELF: this checkout's build in ~/.cache/tfx-build)

2026-10-21: each emulator crash took five or six manual steps to read; this is those steps."""
import bisect, glob, os, re, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.join(here, '..', '..')
prog = os.path.join(repo, 'hw-probe', 'exlaunch', 'source', 'program')
log = open(sys.argv[1], errors='ignore').read().split('\n')
elf = sys.argv[2] if len(sys.argv) > 2 else None
if not elf:
    cands = sorted(glob.glob(os.path.expanduser(f'~/.cache/tfx-build/{os.path.basename(os.path.realpath(repo))}-*/*.elf')), key=os.path.getmtime)
    elf = cands[-1] if cands else None

names = {}
for f in [os.path.join(prog, 'tfex', 'gen', 'tfexgen_binding.hpp')] + glob.glob(os.path.join(prog, '*.cpp')):
    for m in re.finditer(r'(\w+)\s*=\s*(0x[0-9a-f]{5,7})\b', open(f, errors='ignore').read()):
        v = int(m.group(2), 16)
        if 0x80000 < v < 0x3000000: names.setdefault(v, m.group(1))
keys = sorted(names)

start = next((i for i, l in enumerate(log) if 'Guest stack trace' in l), None)
if start is None: sys.exit('no guest stack trace in the log')
for l in log[start + 3:start + 40]:
    m = re.match(r'\s+0x[0-9a-f]+\s+(\S+):(0x[0-9a-f]+)\s*(\S*)', l)
    if not m: break
    mod, off, sym = m.group(1), int(m.group(2), 16), m.group(3)
    if mod == 'TowerFall.nss':
        i = bisect.bisect_right(keys, off) - 1
        print(f'  game +{off:#x}  ~ {names[keys[i]]} +{off - keys[i]:#x}' if i >= 0 else f'  game +{off:#x}')
    elif mod.startswith('nnSdk'):
        print(f'  sdk  +{off:#x}  {sym}')
    elif elf:
        out = subprocess.run(['/opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line', '-f', '-C', '-i', '-e', elf, hex(off - 4)],
                             capture_output=True, text=True).stdout.split('\n')
        pairs = [(out[k], re.sub(r'.*/source/', 'source/', out[k + 1])) for k in range(0, len(out) - 1, 2)]
        print(f'  ours +{off:#x}  ' + ' <- '.join(f'{fn[:90]} ({loc})' for fn, loc in pairs))
print('last [hw] lines before the crash:')
for l in [l for l in log[:start] if '[hw]' in l][-8:]: print('  ' + l[l.index('[hw]'):][:200])

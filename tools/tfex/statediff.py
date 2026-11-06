#!/usr/bin/env python3
"""statediff.py: step E of docs/tfstate-rollback-measurement.md, the PC oracle. Compares TF.State's serialized game state of the same
frames from two logs (the Switch's hwlog, the browser page's console) field by field, with the models' names.

The dump format, the same on both sides (one line per dumped frame; the PC's TF.EX writes it where it serializes a state):
    [state] dump frame <N> [part <k>/<n>] <base64 of the bytes MessagePackSerializer gives, LZ4 block array or plain>
(a long state in parts of whole base64 quads, joined in order)
The bytes are decoded here (MessagePack, MessagePack-CSharp's Lz4BlockArray with its LZ4 blocks) and walked with the schema read
from TF.State's models ([MessagePackObject] classes, [Key(n)] members, [Union(n, typeof(T))]); no dependency.

  statediff.py <switch log> <pc log or .tow replay> [--frames 120,240] [--float-ulps 0] [--src <TF.EX src>]
  statediff.py --mods <log>   (the mods' entries in the dumped states, and their entities' count; a mod's entities are split
  one per entry in the comparison too: AdditionnalData[entities:<mod>-<name>][i])
(A TF.EX replay holds every keyframe's state: the browser page's replay of the same lobby match is the PC side as it is.
Frames line up on GameState.Frame inside each state.)
  statediff.py --self-test     (the decoder on known encodings)
Exit 0 when every common frame is identical."""
import os, re, sys, glob, base64, struct, collections

# ---- MessagePack (the subset MessagePack-CSharp writes) ----
class Ext:
    def __init__(self, code, data): self.code, self.data = code, data
    def __repr__(self): return f'Ext({self.code}, {len(self.data)} bytes)'

def unpack(b, i=0):
    t = b[i]
    if t <= 0x7f: return t, i + 1
    if t >= 0xe0: return t - 0x100, i + 1
    if 0x80 <= t <= 0x8f: return _map(b, i + 1, t & 0x0f)
    if 0x90 <= t <= 0x9f: return _arr(b, i + 1, t & 0x0f)
    if 0xa0 <= t <= 0xbf: n = t & 0x1f; return b[i + 1:i + 1 + n].decode('utf-8', 'replace'), i + 1 + n
    if t == 0xc0: return None, i + 1
    if t == 0xc2: return False, i + 1
    if t == 0xc3: return True, i + 1
    fixed = {0xcc: '>B', 0xcd: '>H', 0xce: '>I', 0xcf: '>Q', 0xd0: '>b', 0xd1: '>h', 0xd2: '>i', 0xd3: '>q', 0xca: '>f', 0xcb: '>d'}
    if t in fixed:
        fmt = fixed[t]; n = struct.calcsize(fmt); v = struct.unpack_from(fmt, b, i + 1)[0]
        if t == 0xca: v = ('f32', v, b[i + 1:i + 5].hex())
        if t == 0xcb: v = ('f64', v, b[i + 1:i + 9].hex())
        return v, i + 1 + n
    if t in (0xc4, 0xc5, 0xc6):
        w = {0xc4: 1, 0xc5: 2, 0xc6: 4}[t]; n = int.from_bytes(b[i + 1:i + 1 + w], 'big'); s = i + 1 + w
        return bytes(b[s:s + n]), s + n
    if t in (0xd9, 0xda, 0xdb):
        w = {0xd9: 1, 0xda: 2, 0xdb: 4}[t]; n = int.from_bytes(b[i + 1:i + 1 + w], 'big'); s = i + 1 + w
        return b[s:s + n].decode('utf-8', 'replace'), s + n
    if t in (0xdc, 0xdd): w = 2 if t == 0xdc else 4; return _arr(b, i + 1 + w, int.from_bytes(b[i + 1:i + 1 + w], 'big'))
    if t in (0xde, 0xdf): w = 2 if t == 0xde else 4; return _map(b, i + 1 + w, int.from_bytes(b[i + 1:i + 1 + w], 'big'))
    if t in (0xd4, 0xd5, 0xd6, 0xd7, 0xd8):
        n = {0xd4: 1, 0xd5: 2, 0xd6: 4, 0xd7: 8, 0xd8: 16}[t]; code = struct.unpack_from('b', b, i + 1)[0]
        return Ext(code, bytes(b[i + 2:i + 2 + n])), i + 2 + n
    if t in (0xc7, 0xc8, 0xc9):
        w = {0xc7: 1, 0xc8: 2, 0xc9: 4}[t]; n = int.from_bytes(b[i + 1:i + 1 + w], 'big'); code = struct.unpack_from('b', b, i + 1 + w)[0]
        s = i + 2 + w; return Ext(code, bytes(b[s:s + n])), s + n
    raise ValueError(f'msgpack: type byte {t:#x} at {i}')

def _arr(b, i, n):
    out = []
    for _ in range(n): v, i = unpack(b, i); out.append(v)
    return out, i

def _map(b, i, n):
    out = {}
    for _ in range(n):
        k, i = unpack(b, i); v, i = unpack(b, i)
        out[k if not isinstance(k, tuple) else k[1]] = v
    return out, i

def lz4_block(src, size):
    """LZ4 block decompression (the format MessagePack-CSharp's LZ4Codec writes)"""
    out = bytearray(); i = 0
    while i < len(src):
        tok = src[i]; i += 1
        n = tok >> 4
        if n == 15:
            while True:
                x = src[i]; i += 1; n += x
                if x != 255: break
        out += src[i:i + n]; i += n
        if i >= len(src): break
        off = src[i] | src[i + 1] << 8; i += 2
        m = tok & 15
        if m == 15:
            while True:
                x = src[i]; i += 1; m += x
                if x != 255: break
        m += 4
        for _ in range(m): out.append(out[-off])
    if len(out) != size: raise ValueError(f'lz4: {len(out)} bytes, expected {size}')
    return bytes(out)

def raw_msgpack(b):
    """the plain MessagePack bytes of a payload: Lz4BlockArray ([array(n+1), ext 98 (lengths), bin per block]) or as it is"""
    if b and 0x90 <= b[0] <= 0x9f or b[:1] in (b'\xdc', b'\xdd'):
        try:
            arr, end = unpack(b, 0)
            if arr and isinstance(arr[0], Ext) and arr[0].code == 98:
                lens, j, d = [], 0, arr[0].data
                while j < len(d): v, j = unpack(d, j); lens.append(v)
                return b''.join(lz4_block(blk, n) for blk, n in zip(arr[1:], lens))
        except Exception:
            pass
    return b

# ---- the schema: TF.State's models ----
def schema(src):
    """{class: {"keys": {n: (member, type)}, "union": {tag: class}}} from the [MessagePackObject] classes under TF.State.Domain"""
    out = {}
    for f in glob.glob(os.path.join(src, 'state', 'TF.State.Domain', '**', '*.cs'), recursive=True):
        text = open(f, encoding='utf-8-sig', errors='replace').read()
        for cm in re.finditer(r'((?:\[[^\]]+\]\s*)*)public\s+(?:abstract\s+|sealed\s+|partial\s+)*(?:class|struct|record)\s+(\w+)\s*(?::\s*(\w+))?[^{]*\{', text):
            name = cm.group(2); base = cm.group(3); body_start = cm.end(); depth = 1; j = body_start
            while depth and j < len(text):
                depth += {'{': 1, '}': -1}.get(text[j], 0); j += 1
            body = text[body_start:j]
            keys = {int(m.group(1)): (m.group(3), m.group(2).strip()) for m in
                    re.finditer(r'\[Key\((\d+)\)\]\s*public\s+([\w<>\[\],.? ]+?)\s+(\w+)\s*\{', body)}
            union = {int(m.group(1)): m.group(2) for m in re.finditer(r'\[Union\((\d+),\s*typeof\((\w+)\)\)\]', cm.group(1))}
            if keys or union or base: out.setdefault(name, {'keys': keys, 'union': union, 'base': base})
    for name, d in out.items():   # a subclass's keys include its bases' (DefaultArrow : Arrow)
        b = d.get('base'); seen = 0
        while b in out and seen < 8:
            for k, v in out[b]['keys'].items(): d['keys'].setdefault(k, v)
            b = out[b].get('base'); seen += 1
    return out

def walk(sch, ty, v, path, out):
    """flatten v (decoded MessagePack) to {path: scalar} with member names, following the declared type ty"""
    ty = (ty or '').strip().rstrip('?')
    base = re.sub(r'^[\w.]+\.', '', ty)
    g = re.match(r'(?:List|IEnumerable|IList|ICollection|IReadOnlyList|HashSet)<(.+)>$', base) or re.match(r'(.+)\[\]$', base)
    if isinstance(v, list) and base in sch and sch[base]['union'] and len(v) == 2 and isinstance(v[0], int):
        walk(sch, sch[base]['union'].get(v[0], '?'), v[1], f'{path}<{sch[base]["union"].get(v[0], v[0])}>', out); return
    if isinstance(v, list) and base in sch and sch[base]['keys']:
        for k, x in enumerate(v):
            m, t = sch[base]['keys'].get(k, (f'#{k}', ''))
            walk(sch, t, x, f'{path}.{m}' if path else m, out)
        return
    if isinstance(v, list):
        el = g.group(1) if g else ''
        out[f'{path}.Count'] = len(v)
        for k, x in enumerate(v): walk(sch, el, x, f'{path}[{k}]', out)
        return
    if isinstance(v, dict):
        dm = re.match(r'(?:Dictionary|IDictionary|IReadOnlyDictionary)<([^,]+),\s*(.+)>$', base)
        for k in sorted(v, key=str):
            if str(k).startswith('entities:') and isinstance(v[k], str):   # a mod's entities (TF.State ModEntities): one entry each
                for i, e in enumerate(v[k].split(';')): out[f'{path}[{k}][{i}]'] = e
                out[f'{path}[{k}].Count'] = len(v[k].split(';')) if v[k] else 0
                continue
            walk(sch, dm.group(2) if dm else '', v[k], f'{path}[{k}]', out)
        return
    out[path] = v

TAGS = ('dump', 'dump')   # (--tags A B: side A's line tag and side B's, e.g. dump / dump2 for a round trip's before and after in one log)
def frames(log, tag='dump'):
    """{frame: plain MessagePack bytes}: "[state] dump frame <N> [part <k>/<n>] <base64>" lines, parts joined in order"""
    parts = collections.defaultdict(dict); total = {}
    for line in open(log, errors='replace'):
        m = re.search(r'\[state\] ' + tag + r' frame (\d+)(?: part (\d+)/(\d+))? ([A-Za-z0-9+/=]+)', line)
        if m:
            fr = int(m.group(1)); parts[fr][int(m.group(2) or 1)] = m.group(4); total[fr] = int(m.group(3) or 1)
    return {fr: raw_msgpack(base64.b64decode(''.join(p[k] for k in sorted(p)))) for fr, p in parts.items() if len(p) == total[fr]}

def raw_frames(log, tag='dump'):
    """{frame: the saved bytes as written (MessagePack's LZ4 block array: what GGRS's checksum reads)}"""
    parts = collections.defaultdict(dict); total = {}
    for line in open(log, errors='replace'):
        m = re.search(r'\[state\] ' + tag + r' frame (\d+)(?: part (\d+)/(\d+))? ([A-Za-z0-9+/=]+)', line)
        if m:
            fr = int(m.group(1)); parts[fr][int(m.group(2) or 1)] = m.group(4); total[fr] = int(m.group(3) or 1)
    return {fr: base64.b64decode(''.join(p[k] for k in sorted(p))) for fr, p in parts.items() if len(p) == total[fr]}

def replay_frames(path):
    """{frame: plain MessagePack bytes} from a TF.EX replay file (.tow: the Replay as MessagePack, LZ4 block array; Record = [Inputs,
    State, Frame], State on keyframes only)"""
    rep, _ = unpack(raw_msgpack(open(path, 'rb').read()))
    out = {}
    for rec in (rep[1] if isinstance(rep, list) and len(rep) > 1 and isinstance(rep[1], list) else []):
        if isinstance(rec, list) and len(rec) > 2 and isinstance(rec[1], (bytes, bytearray)): out[rec[2]] = raw_msgpack(bytes(rec[1]))
    return out

def by_state_frame(fr):
    """re-key by GameState.Frame (key 10) inside each state, where it decodes: both sides then line up on the game's own frame"""
    out = {}
    for k, b in fr.items():
        try:
            v, _ = unpack(b)
            out[v[10] if isinstance(v, list) and len(v) > 10 and isinstance(v[10], int) else k] = b
        except Exception:
            out[k] = b
    return out

def load(path, tag='dump'):
    return by_state_frame(replay_frames(path) if path.endswith('.tow') else frames(path, tag))

def same(a, b, ulps):
    if isinstance(a, tuple) and isinstance(b, tuple) and a[0] == b[0]:
        if a[2] == b[2]: return True
        if not ulps: return False
        fmt = '>i' if a[0] == 'f32' else '>q'
        ia, ib = struct.unpack(fmt, bytes.fromhex(a[2]))[0], struct.unpack(fmt, bytes.fromhex(b[2]))[0]
        return abs(ia - ib) <= ulps
    return a == b

def show(v): return f'{v[1]!r} ({v[2]})' if isinstance(v, tuple) else repr(v)

def self_test():
    assert unpack(bytes([0x93, 0x01, 0xc3, 0xa2]) + b'hi')[0] == [1, True, 'hi']
    assert unpack(bytes.fromhex('ca3f800000'))[0][1] == 1.0
    assert unpack(bytes.fromhex('d1ff00'))[0] == -256
    assert unpack(bytes.fromhex('82a161 01 a162 c0'.replace(' ', '')))[0] == {'a': 1, 'b': None}
    lit = bytes([0x40]) + b'abcd'                 # 4 literals, no match: the last sequence
    assert lz4_block(lit, 4) == b'abcd'
    seq = bytes([0x10]) + b'a' + bytes([1, 0]) + bytes([0x10]) + b'b'   # 'a', then a match of 4 at offset 1, then 'b'
    assert lz4_block(seq, 6) == b'aaaaab'
    print('statediff: self-test ok')

def mods(path):
    """--mods <log>: the mods' entries in the dumped states (AdditionnalData: a mod's state by key, its entities as "entities:" keys):
    on how many dumps each was, and its entities' count; one summary line first"""
    fr = load(path); seen = collections.Counter(); ents = collections.defaultdict(list)
    for f in sorted(fr):
        try: ad = unpack(fr[f])[0][9] or {}
        except Exception: continue
        for k, x in ad.items():
            seen[k] += 1
            if str(k).startswith('entities:'): ents[k].append(len(x.split(';')) if x else 0)
    print(', '.join(f'{k} on {n}/{len(fr)}' + (f' ({min(ents[k])}-{max(ents[k])} entities)' if k in ents else '') for k, n in sorted(seen.items()))
          or f'no mod entries in {len(fr)} dumps')
    return 0

def main(argv):
    if '--self-test' in argv: self_test(); return 0
    if '--mods' in argv: return mods([x for x in argv if not x.startswith('--')][0])
    valued = {'--src', '--tags', '--frames', '--float-ulps', '--before'}   # options followed by a value (not a file)
    a = [x for i, x in enumerate(argv) if not x.startswith('--') and not (i > 0 and argv[i - 1] in valued)]
    opt = lambda k, d=None: next((argv[i + 1] for i, x in enumerate(argv) if x == k), d)
    # the models of the pinned TF.EX (vendor/tfex); not the first cached tree, which may be an older tag's (v0.27.0's MatchStats unnamed)
    src = opt('--src') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vendor', 'tfex', 'src')
    sch = schema(src)
    tg = opt('--tags', 'dump,dump').split(',')
    sw, pc = load(a[0], tg[0]), load(a[1], tg[1] if len(tg) > 1 else tg[0])
    before = int(opt('--before', '0'))   # only frames before it (smoke.sh: a match's last second may never be confirmed on the page)
    want = [int(x) for x in opt('--frames', '').split(',') if x] or sorted(f for f in set(sw) & set(pc) if not before or f < before)
    ulps = int(opt('--float-ulps', '0'))
    print(f'statediff: schema {len(sch)} classes; frames: Switch {len(sw)}, PC {len(pc)}, compared {len(want)}')
    bad = 0
    for fr in want:
        if fr not in sw or fr not in pc: print(f'  frame {fr}: missing on the {"Switch" if fr not in sw else "PC"}'); bad += 1; continue
        if sw[fr] == pc[fr]: print(f'  frame {fr}: identical ({len(sw[fr])} bytes)'); continue
        fa, fb = {}, {}
        walk(sch, 'GameState', unpack(sw[fr])[0], '', fa); walk(sch, 'GameState', unpack(pc[fr])[0], '', fb)
        diffs = [k for k in sorted(set(fa) | set(fb)) if not same(fa.get(k, '<absent>'), fb.get(k, '<absent>'), ulps)]
        bad += 1 if diffs else 0
        print(f'  frame {fr}: {len(diffs)} fields differ' + (' (within the float tolerance otherwise)' if not diffs else ''))
        for k in diffs[:60]: print(f'    {k}: Switch {show(fa.get(k, "<absent>"))}  PC {show(fb.get(k, "<absent>"))}')
    if '--bytes' in argv:   # the compressed bytes too (GGRS checksums these): same state, other LZ4 blocks = a checksum desync
        rs, rp = raw_frames(a[0], tg[0]), raw_frames(a[1], tg[1] if len(tg) > 1 else tg[0])
        both = sorted(f for f in set(rs) & set(rp) if not before or f < before); diff = [f for f in both if rs[f] != rp[f]]
        print(f'statediff: saved bytes identical on {len(both) - len(diff)} of {len(both)} frames' + (f'; differ at {diff}' if diff else ''))
        bad += sum(1 for f in diff if f > 0)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

"""TF.EX update report: what changed between two TF.EX tags, where it lands in our port, and which class (A-E) it is.
Joins the splitter's member lists (split.sh) with manifest.toml (what we ported, from which body hash), overrides.toml (the
TFEX-BASE.md section 3 ledger as data) and rules.toml (the A-E labels). Run through update.sh; see README.md.

update.py --old-tag T --new-tag T --old J --new J --old-src D --new-src D [--targets-old F --targets-new F] [--expect F]
"""
import argparse, difflib, fnmatch, hashlib, json, os, re, sys, tomllib
from collections import defaultdict, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
CLASSES = "ABCDE?"
CLASS_NAME = {"A": "lockstep", "B": "protocol", "C": "UI", "D": "not ours", "E": "noise", "?": "unlabelled"}


def load_split(path):
    recs = OrderedDict()
    for line in open(path, encoding="utf-8"):
        r = json.loads(line)
        recs[(r["file"], r["type"], r["member"])] = r
    return recs


def full(r):
    return r["type"] + "." + r["member"]


def bare(r):
    return full(r).split("(")[0]


# ---- patterns: "<file glob> <member glob>" ----
def parse_key(key):
    f, _, m = key.strip().partition(" ")
    return f, (m.strip() or "*")


def match(rec, key):
    fp, mp = parse_key(key)
    if not fnmatch.fnmatchcase(rec["file"], fp):
        return False
    if mp == "*":
        return True
    esc = mp.replace("[", "[[]") if "[" in mp and "]" in mp and "(" in mp else mp
    if "(" in mp:
        return fnmatch.fnmatchcase(full(rec), esc)
    return fnmatch.fnmatchcase(bare(rec), esc) or fnmatch.fnmatchcase(full(rec), esc) or \
        (mp.endswith(".*") and fnmatch.fnmatchcase(rec["type"], mp[:-2]))


def combined_hash(recs):
    h = hashlib.sha1()
    for r in sorted(recs, key=lambda r: (r["file"], full(r))):
        h.update(f"{r['file']} {full(r)} {r['norm']}\n".encode())
    return h.hexdigest()[:12]


def entry_hash(split, key):
    rs = [r for r in split.values() if match(r, key)]
    return (combined_hash(rs) if rs else None), rs


# ---- changes between two splits ----
def changes(old, new):
    out = []
    removed = {k: r for k, r in old.items() if k not in new}
    added = {k: r for k, r in new.items() if k not in old}
    for k in old.keys() & new.keys():
        a, b = old[k], new[k]
        if a["strict"] != b["strict"]:
            out.append(dict(status="syntax" if a["norm"] == b["norm"] else "changed", old=a, new=b))
    # pair a removed and an added member: same file+type+name (signature changed), else same type+member elsewhere (moved)
    by_name = defaultdict(list)
    for k, r in added.items():
        by_name[(r["file"], bare(r))].append(k)
    by_member = defaultdict(list)
    for k, r in added.items():
        by_member[full(r)].append(k)
    for k, a in sorted(removed.items()):
        cand = [x for x in by_name.get((a["file"], bare(a)), []) if x in added]
        status = "signature"
        if not cand:
            cand = [x for x in by_member.get(full(a), []) if x in added]
            status = "moved"
        if cand:
            b = added.pop(cand[0])
            out.append(dict(status=status if a["norm"] != b["norm"] or status == "moved" else "syntax", old=a, new=b))
        else:
            out.append(dict(status="removed", old=a, new=None))
    for k, b in added.items():
        out.append(dict(status="added", old=None, new=b))
    for c in out:
        r = c["new"] or c["old"]
        c["file"], c["type"], c["member"], c["kind"] = r["file"], r["type"], r["member"], r["kind"]
        c["rec"] = r
    out.sort(key=lambda c: (c["file"], c["type"], c["member"]))
    return out


def hunks(c, ctx=0):
    """The changed token runs (old tokens | new tokens), each with ctx tokens of context before it."""
    if c["old"] is None or c["new"] is None:
        return [(c["new"] or c["old"])["text"]]
    a, b = c["old"]["text"].split(" "), c["new"]["text"].split(" ")
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return [" ".join(a[max(0, i1 - ctx):i1] + a[i1:i2] + ["|"] + b[j1:j2]) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal"]


def changed_text(c):
    if c["old"] is None:
        return c["new"]["text"]
    if c["new"] is None:
        return c["old"]["text"]
    a, b = c["old"]["text"].split(" "), c["new"]["text"].split(" ")
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    parts = []
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op != "equal":
            parts += a[i1:i2] + ["|"] + b[j1:j2] + ["|"]
    return " ".join(parts)


def labels(c, rules):
    if c["status"] == "syntax":
        return [("E", "syntax-only change (normalized body unchanged)")]
    text = None
    got = []
    for r in rules:
        files = r["file"] if isinstance(r["file"], list) else [r["file"]]
        if not any(fnmatch.fnmatchcase(c["file"], f) for f in files):
            continue
        if "member" in r and not fnmatch.fnmatchcase(full(c["rec"]), r["member"]):
            continue
        if "status" in r and c["status"] not in r["status"]:
            continue
        if "kind" in r and c["kind"] not in r["kind"]:
            continue
        if "only" in r:
            toks = [t for t in changed_text(c).split(" ") if t not in ("|", "")]
            if c["old"] is None or c["new"] is None or not toks or not all(re.fullmatch(r["only"], t) for t in toks):
                continue
        if "hunks" in r and (c["old"] is None or c["new"] is None or not all(re.search(r["hunks"], h) for h in hunks(c, 4))):
            continue
        if "diff" in r:
            if text is None:
                text = " || ".join(hunks(c, 3))
            if not re.search(r["diff"], text):
                continue
        if r["class"] not in [g[0] for g in got]:
            got.append((r["class"], r.get("why", "")))
        if r.get("stop"):
            break
    if not got:
        got = [("?", "no rule matched")]
    got.sort(key=lambda g: CLASSES.index(g[0]))
    return got


def source_hunk(c, old_src, new_src, limit=40):
    def lines(rec, root):
        if rec is None:
            return []
        try:
            src = open(os.path.join(root, rec["file"]), encoding="utf-8-sig", errors="replace").read().split("\n")
        except OSError:
            return []
        return src[rec["line"] - 1:rec["end"]]
    a, b = lines(c["old"], old_src), lines(c["new"], new_src)
    d = list(difflib.unified_diff(a, b, lineterm="", n=2))[2:]
    if len(d) > limit:
        d = d[:limit] + [f"... ({len(d) - limit} more diff lines)"]
    return "\n".join(d)


def md_escape(s):
    return s.replace("|", "\\|")


def loc(rec, tag):
    return f"{rec['file']}:{rec['line']}" if rec else ""


def read_targets(path):
    """patch-targets.sh output with TFEX_ALL=1: '<status> <Type> <method>' lines -> {(type, method): status}."""
    out = {}
    if not path or not os.path.exists(path):
        return None, ""
    summary = ""
    for line in open(path):
        p = line.split()
        if line.startswith("pairs "):
            summary = line.strip()
        elif len(p) == 3 and p[0] in ("found", "nomethod", "notype", "nongame"):
            out[(p[1].split(".")[-1], p[2])] = (p[0], p[1])
    return out, summary


def load_members(gen_dir):
    """tfexgen_members.txt in a translator output folder: file -> [(first line, last line, "Type.Member")], the TF.EX members translated
    without a blocker. A change to one of them reaches the Switch by re-running the translator: no port work."""
    out = defaultdict(list)
    p = os.path.join(gen_dir, "tfexgen_members.txt") if gen_dir else None
    if p and os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            f, a, b, m = line.rstrip("\n").split("\t")
            out[f].append((int(a), int(b), m))
    return out


def generated(rec, gm):
    """rec (a splitter record) is translated: a translated member's lines overlap it, or, for a type's header, a member of that type is."""
    if not rec:
        return False
    ms = gm.get(rec["file"], ())
    if rec["member"] == "<type>":
        return any(m.startswith(rec["type"] + ".") for _, _, m in ms)
    return any(x <= rec["end"] and y >= rec["line"] for x, y, _ in ms)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old-tag", required=True); ap.add_argument("--new-tag", required=True)
    ap.add_argument("--old", required=True); ap.add_argument("--new", required=True)
    ap.add_argument("--old-src", required=True); ap.add_argument("--new-src", required=True)
    ap.add_argument("--targets-old"); ap.add_argument("--targets-new")
    ap.add_argument("--manifest", default=os.path.join(HERE, "manifest.toml"))
    ap.add_argument("--overrides", default=os.path.join(HERE, "overrides.toml"))
    ap.add_argument("--rules", default=os.path.join(HERE, "rules.toml"))
    ap.add_argument("--expect", help="expected labels (tools/tfex/expect/*.toml): adds a check section")
    ap.add_argument("--gen-old"); ap.add_argument("--gen-new")
    ap.add_argument("--hunks", type=int, default=40, help="max diff lines per member (0: none)")
    ap.add_argument("--json", help="also write what was found as JSON (new-tag.py's one-page summary reads it)")
    ap.add_argument("--translate", default=os.path.join(HERE, "translate.toml"), help="the translator's units (for --json)")
    a = ap.parse_args()

    old, new = load_split(a.old), load_split(a.new)
    manifest = tomllib.load(open(a.manifest, "rb")).get("port", {}) if os.path.exists(a.manifest) else {}
    ov = tomllib.load(open(a.overrides, "rb")) if os.path.exists(a.overrides) else {}
    skips, replaces, names = ov.get("skip", {}), ov.get("replace", {}), ov.get("names", {})
    rules = tomllib.load(open(a.rules, "rb"))["rule"]

    ch = changes(old, new)
    gm_old, gm_new = load_members(a.gen_old), load_members(a.gen_new)
    # translate.toml [[replace]]: the translator emits a glue body instead of TF.EX's (a ledger difference like overrides [replace])
    tr_rep = {}
    if os.path.exists(a.translate):
        for r in tomllib.load(open(a.translate, "rb")).get("replace", []):
            t, _, mname = r["member"].rpartition(".")
            tr_rep[(t.rpartition(".")[2], mname)] = r
    def tr_replaced(rec):
        return [f"translate.toml [[replace]] {r['member']}" for (t, mn), r in tr_rep.items()
                if rec and rec["type"].split(".")[-1].split("<")[0] == t and rec["member"].split("(")[0] == mn]
    for c in ch:
        c["labels"] = labels(c, rules)
        c["cls"] = c["labels"][0][0]
        recs = [r for r in (c["old"], c["new"]) if r]
        c["ported"] = [k for k in manifest if any(match(r, k) for r in recs)]
        c["replaced"] = [k for k in replaces if any(match(r, k) for r in recs)] + sorted({x for r in recs for x in tr_replaced(r)})
        c["skipped"] = [k for k in skips if any(match(r, k) for r in recs)]
        if c["status"] == "syntax":
            c["disp"] = "noise"
        elif c["replaced"]:
            c["disp"] = "override"
        elif generated(c["old"], gm_old) or generated(c["new"], gm_new):
            c["disp"] = "generated"
        elif c["ported"]:
            c["disp"] = "stale"
        elif c["replaced"]:
            c["disp"] = "override"
        elif c["skipped"]:
            c["disp"] = "skipped"
        else:
            c["disp"] = "new" if c["status"] == "added" else "unported"

    # manifest entries: stale when the new tag's body hash differs from the one the port was made from
    stale = []
    for key, e in manifest.items():
        h_new, rs_new = entry_hash(new, key)
        h_old, _ = entry_hash(old, key)
        base = e.get("hash") or h_old
        if h_new != base:
            items = [c for c in ch if key in c["ported"] and c["status"] != "syntax"]
            stale.append(dict(key=key, e=e, base=base, h_old=h_old, h_new=h_new, items=items,
                              note=("gone at " + a.new_tag) if h_new is None else
                                   (f"manifest hash {base} is not {a.old_tag}'s ({h_old}): the port predates {a.old_tag}" if e.get("hash") and e["hash"] != h_old else "")))
    review = []
    for key in sorted({k for c in ch for k in c["replaced"] if k.startswith("translate.toml")}):
        items = [c for c in ch if key in c["replaced"] and c["status"] != "syntax"]
        r = tr_rep[next(k for k, v in tr_rep.items() if key.endswith(v["member"]))]
        if items:
            review.append(dict(key=key, e=dict(key=r.get("key", ""), at=r.get("to", "")), items=items))
    for key, e in replaces.items():
        if not isinstance(e, dict):
            continue
        h_new, _ = entry_hash(new, key)
        base = e.get("hash") or entry_hash(old, key)[0]
        if h_new != base:
            review.append(dict(key=key, e=e, items=[c for c in ch if key in c["replaced"] and c["status"] != "syntax"]))

    W = []
    w = W.append
    real = [c for c in ch if c["status"] != "syntax"]
    w(f"# TF.EX update report: {a.old_tag} → {a.new_tag}\n")
    w(f"Generated by `tools/tfex/update.sh {a.old_tag} {a.new_tag}`. Members: {len(old)} → {len(new)}; "
      f"{len(ch)} changed members ({len(real)} real, {len(ch) - len(real)} syntax-only) in "
      f"{len(set(c['file'] for c in ch))} files. Manifest: {len(manifest)} entries, overrides: {len(skips)} skip, {len(replaces)} replace.\n")
    w("Classes: **A** lockstep (desync if it differs), **B** protocol (server messages), **C** UI, **D** not ours, **E** noise, "
      "**?** no rule (triage by hand). Dispositions: *stale* = a ported member changed; *override* = a replaced member changed "
      "(check the replacement still fits); *new* / *unported* = no port and no skip: work, or a skip to add; *skipped* = ledger says ignore.\n")

    # summary
    w("## Summary\n")
    w("| class | generated | stale | override | new | unported | skipped | noise | total |\n|---|---|---|---|---|---|---|---|---|")
    for k in CLASSES:
        row = [c for c in ch if c["cls"] == k]
        if not row:
            continue
        cnt = lambda d: sum(1 for c in row if c["disp"] == d)
        w(f"| {k} {CLASS_NAME[k]} | {cnt('generated')} | {cnt('stale')} | {cnt('override')} | {cnt('new')} | {cnt('unported')} | {cnt('skipped')} | {cnt('noise')} | {len(row)} |")
    w("")

    def item_line(c, with_disp=False):
        labs = "+".join(l[0] for l in c["labels"])
        why = "; ".join(l[1] for l in c["labels"] if l[1])
        where = c["new"] or c["old"]
        extra = ""
        if c["status"] in ("signature", "moved"):
            extra = f" (was `{md_escape(full(c['old']))}`" + (f" in {c['old']['file']}" if c["status"] == "moved" else "") + ")"
        patch = f" — patches `{where['patch']}`" if where.get("patch") else ""
        disp = f" [{c['disp']}]" if with_disp else ""
        return f"- **{labs}** {c['status']}{disp} `{md_escape(full(where))}`{extra}{patch} — {c['file']}:{where['line']}  \n  _{md_escape(why)}_"

    callers_idx = {}

    def callers(name):
        """Members of the new tag whose body uses `name` (as a call, member access or type)."""
        if name not in callers_idx:
            pat = re.compile(r"(^|[ .])" + re.escape(name) + r"( [(.<;,)]| ?$| =| \[)")
            callers_idx[name] = [r for r in new.values() if r["member"] != "<type>" and pat.search(r["text"])]
        return callers_idx[name]

    def near_places(c):
        """Where an unported member would land: our port of its upstream callers, else our places for its type."""
        name = c["member"].split("(")[0]
        if name == "<type>":
            name = c["type"].split(".")[-1].split("<")[0]
        places, via = [], []
        tname = c["type"].split(".")[-1].split("<")[0]
        for r in callers(name):
            if r is c["new"]:
                continue
            if c["member"] != "<type>" and r["type"] != c["type"] and tname not in r["text"]:
                continue   # a common member name (RoomPeerId): only callers that also name the type
            for k, e in manifest.items():
                if match(r, k):
                    for p in e.get("at", "?").split(";"):
                        p = p.strip()
                        if p and p not in places:
                            places.append(p); via.append(bare(r))
        how = "used by ported " + ", ".join(sorted(set(via))[:3]) if places else ""
        if not places:
            for k, e in manifest.items():
                fp, mp = parse_key(k)
                if "*" not in fp and fnmatch.fnmatchcase(c["file"], fp) and mp.split(".")[0] == c["type"].split(".")[0]:
                    for p in e.get("at", "?").split(";"):
                        p = p.strip()
                        if p and p not in places:
                            places.append(p)
            how = "same type"
        return places, how

    def near(c):
        places, how = near_places(c)
        if places:
            w(f"  → lands near ({how}): " + "; ".join(f"`{p}`" for p in places[:4]) + (" …" if len(places) > 4 else ""))

    def hunk(c):
        if a.hunks and c["status"] in ("changed", "signature", "moved"):
            h = source_hunk(c, a.old_src, a.new_src, a.hunks)
            if h:
                w("  <details><summary>diff</summary>\n\n```diff\n" + h + "\n```\n  </details>")

    for s_ in stale:
        s_["gen"] = bool(s_["items"]) and all(c["disp"] == "generated" for c in s_["items"])
    obsolete = [s_ for s_ in stale if s_["gen"]]
    stale = [s_ for s_ in stale if not s_["gen"]]
    behind = [s for s in stale if not s["items"] and s["h_new"] is not None]
    stale = [s for s in stale if s["items"] or s["h_new"] is None]
    w("## 0. Generated: follows by re-running the translator\n")
    gen_ch = [c for c in ch if c["disp"] == "generated"]
    w(f"{len(gen_ch)} changed members are TF.EX code the translator emits (tfexgen_members.txt at either tag): `tools/tfex/translate.sh all "
      f"{a.new_tag}` brings them, section 9 shows the C++ diff. Nothing to port; read the A items when a match desyncs.\n")
    byf = defaultdict(list)
    for c in gen_ch:
        byf[(c["cls"], c["file"])].append(c)
    for (k, f), cs in sorted(byf.items(), key=lambda kv: (CLASSES.index(kv[0][0]), kv[0][1])):
        w(f"- [{k}] {f}: {len(cs)} ({', '.join(sorted(set(c['status'] for c in cs)))})")
    w("")
    if obsolete:
        w("Manifest entries whose changes are all generated (the hand port they name is gone; delete them):\n")
        for s_ in obsolete:
            w(f"- `{s_['key']}` (ours: `{s_['e'].get('at', '?')}`)")
        w("")

    w("## 1. Stale: ported members that changed upstream\n")
    if not stale:
        w("None.\n")
    for s in sorted(stale, key=lambda s: min([CLASSES.index(c["cls"]) for c in s["items"]] or [5])):
        e = s["e"]
        cls = "+".join(sorted(set(c["cls"] for c in s["items"]), key=CLASSES.index)) or "?"
        w(f"### [{cls}] `{s['key']}`\n")
        w(f"Ours: `{e.get('at', '?')}` (confidence {e.get('conf', '?')})" + (f" — {e['note']}" if e.get("note") else "") + "  ")
        if s["note"]:
            w(f"Note: {s['note']}  ")
        w("")
        for c in s["items"]:
            w(item_line(c)); hunk(c)
        w("")

    if behind:
        w(f"### Still behind (changed before {a.old_tag}, not in this range)\n")
        w("The port matches an older TF.EX body (the manifest hash); see the report from that tag:\n")
        for s in behind:
            w(f"- `{s['key']}` — ours `{s['e'].get('at', '?')}`")
        w("")

    w("## 2. Overrides to review: replaced members that changed upstream\n")
    if not review:
        w("None.\n")
    for r in review:
        w(f"### `{r['key']}` → {r['e'].get('key', '')} `{r['e'].get('at', '')}`\n")
        for c in r["items"]:
            w(item_line(c)); hunk(c)
        w("")

    w("## 3. New and unported changes (no manifest entry, no override)\n")
    w("Each is work (port it, then add a manifest entry) or a decision (add a skip with the reason).\n")
    groups = OrderedDict()
    for c in sorted([c for c in ch if c["disp"] in ("new", "unported")], key=lambda c: (CLASSES.index(c["cls"]), c["file"])):
        groups.setdefault((c["cls"], c["file"]), []).append(c)
    last = None
    for (k, f), cs in groups.items():
        if k != last:
            w(f"### {k}: {CLASS_NAME[k]}\n"); last = k
        w(f"**{f}**\n")
        for c in cs:
            w(item_line(c))
            if c["cls"] in "ABC?":
                near(c)
            if c["status"] != "added":
                hunk(c)
        w("")

    w("## 4. Skipped (the ledger says ignore)\n")
    sk = defaultdict(list)
    for c in ch:
        if c["disp"] == "skipped":
            sk[(c["skipped"][0], c["file"])].append(c)
    if not sk:
        w("None.\n")
    for (key, f), cs in sorted(sk.items(), key=lambda kv: kv[0][1]):
        reason = skips[key]
        clss = "+".join(sorted(set(c["cls"] for c in cs), key=CLASSES.index))
        stat = ", ".join(f"{n} {s}" for s, n in sorted(_count(cs, "status").items()))
        w(f"- [{clss}] **{f}**: {stat} — _{md_escape(reason)}_" +
          ("" if len(cs) > 6 else ": " + ", ".join(f"`{md_escape(c['member'])}`" for c in cs)))
    w("")

    w("## 5. Syntax-only (E)\n")
    syn = [c for c in ch if c["status"] == "syntax"]
    w(f"{len(syn)} members whose normalized body is unchanged (C# 12 collection expressions, target-typed `new`, comments, "
      "whitespace): no port work.\n")
    byf = defaultdict(list)
    for c in syn:
        byf[c["file"]].append(c["member"])
    for f, ms in sorted(byf.items()):
        w(f"- {f}: " + ", ".join(f"`{md_escape(m)}`" for m in ms))
    w("")

    # Harmony targets
    w("## 6. Harmony patch targets\n")
    t_old, s_old = read_targets(a.targets_old)
    t_new, s_new = read_targets(a.targets_new)
    p_old = {r["patch"].split(" ")[0] for r in old.values() if r.get("patch")}
    p_new = {r["patch"].split(" ")[0] for r in new.values() if r.get("patch")}
    if s_old or s_new:
        w(f"`patch-targets.sh`: {a.old_tag}: {s_old or 'not run'}; {a.new_tag}: {s_new or 'not run'}.\n")
    if t_old is not None and t_new is not None:
        added_t = sorted(set(t_new) - set(t_old)); gone_t = sorted(set(t_old) - set(t_new))
        w(f"New targets ({len(added_t)}): a new target is a new hook; one missing on 1.4.1.6 is new RE work.\n")
        for k in added_t:
            st, ty = t_new[k]
            who = sorted({f"{r['file'].split('/')[-1]} {r['type']}.{r['member'].split('(')[0]}" for r in new.values()
                          if r.get("patch") and r["patch"].split(" ")[0] in (f"{k[0]}.{k[1]}",)})
            w(f"- `{ty}.{k[1]}`: **{st}**" + (f" — by {', '.join(who)}" if who else ""))
        if gone_t:
            w(f"\nTargets no longer patched ({len(gone_t)}): " + ", ".join(f"`{t}.{m}`" for t, m in gone_t))
        w("")
    else:
        w("Targets added (splitter, no 1.4.1.6 check): " + ", ".join(f"`{p}`" for p in sorted(p_new - p_old)) + "\n")

    # A tests, B server to-do
    w("## 7. Required checks\n")
    aa = [c for c in ch if c["cls"] == "A" and c["disp"] != "noise"]
    if aa:
        w(f"**A (lockstep), {len(aa)} members.** Before a Switch plays TF.EX {a.new_tag}:\n")
        w(f"- a Switch-vs-TF.EX match on {a.new_tag}: `tools/smoke.sh lobby` (a TF.EX lobby match against the browser) with the browser on `DShad.TF.EX-{a.new_tag}.zip`, "
          "covering what the changes touch (random tower / unlocks: a Switch with fewer unlocks than the PC; level order; team and seat "
          "setup from the lobby), digest equal for the whole match;")
        w("- the rollback drill (`rbdrill`) on the entities whose TF.State patch changed;")
        w("- members skipped as S-rollback (TF.State snapshot fixes): no port, the same match is their test.\n")
        files = sorted({c["file"] for c in aa})
        w("Touched: " + ", ".join(f"`{'/'.join(f.split('/')[-3:])}`" for f in files) + "\n")
    bb = [c for c in ch if any(l[0] == "B" for l in c["labels"]) and c["disp"] != "noise"]
    if bb:
        w(f"**B (protocol), {len(bb)} members: tfex-server to-do** (`Towerfall Browser/cloudflare/tfex-server`) and the Switch's "
          "`tfex/Models.*` / `tfex/MatchmakingService.*`:\n")
        models = [c for c in bb if "/Models/WebSocket/" in c["file"]]
        for c in models:
            w(f"- model {c['status']}: `{md_escape(full(c['rec']))}` ({c['file'].split('/')[-1]})")
        lits = set()
        for c in bb:
            # quoted keys ("room_peer_id") and URL paths, also inside interpolated strings ($"{SERVER_URL}/relay/room/{id}")
            new_t = (c["new"] or c["old"])["text"]
            for s in re.findall(r'"([a-z]+_[a-z_]+)"', new_t) + re.findall(r'(?<![\w}])((?:/[a-z_]+)+/?)(?=[ {?"])', new_t):
                if c["old"] is None or s not in c["old"]["text"]:
                    lits.add(s)
        if lits:
            w("- new strings in protocol code (endpoints, keys): " + ", ".join(f"`{s}`" for s in sorted(lits)))
        msgs = sorted({c["type"] for c in models if c["status"] == "added" and c["member"] == "<type>"})
        if msgs:
            w("- new message/model types: " + ", ".join(f"`{m}`" for m in msgs))
        w("")

    if a.gen_old and a.gen_new:
        w(gen_section(a.gen_old, a.gen_new, a.old_tag, a.new_tag))
    if a.expect:
        w(expect_section(a.expect, ch))
    sys.stdout.write("\n".join(W) + "\n")
    if a.json:
        write_json(a, old, new, ch, stale, review, near_places)


def write_json(a, old, new, ch, stale, review, near_places):
    """The findings as data: counts, and every place of ours a change touches (hand-written port, ledger replacement, translated unit,
    service-map entry), for tools/tfex/new-tag.py."""
    real = [c for c in ch if c["status"] != "syntax"]
    tr = tomllib.load(open(a.translate, "rb")) if os.path.exists(a.translate) else {}
    ov = tomllib.load(open(a.overrides, "rb")) if os.path.exists(a.overrides) else {}
    units = []
    for u in tr.get("unit", []):
        hit = [c for c in real if c["file"] == u.get("file") and c["type"].split(".")[0] == u.get("type") and
               (not u.get("members") or c["member"].split("(")[0] in u["members"] or c["member"] == "<type>")]
        if hit:
            units.append(dict(unit=u["name"], file=u["file"], members=sorted({bare(c["new"] or c["old"]) for c in hit})))
    services = []
    for key in ov.get("service", {}):
        typ, mem = key.split(".")[-2:]   # "<Namespace.Type>.<Member>", the member may end in *
        hit = [c for c in real if c["type"].split(".")[-1] == typ and fnmatch.fnmatchcase(c["member"].split("(")[0], mem)]
        if hit:
            services.append(dict(key=key, members=sorted({bare(c["new"] or c["old"]) for c in hit})))
    summary = {k: {d: sum(1 for c in ch if c["cls"] == k and c["disp"] == d) for d in
                   ("generated", "stale", "override", "new", "unported", "skipped", "noise")} for k in CLASSES}
    open_items = []
    for c in real:
        if c["disp"] in ("new", "unported") and c["cls"] in ("A", "B", "C", "?"):
            places, how = near_places(c)
            open_items.append(dict(cls=c["cls"], disp=c["disp"], status=c["status"], member=bare(c["new"] or c["old"]),
                                   file=c["file"], near=places[:4], how=how))
    out = dict(old=a.old_tag, new=a.new_tag, members=[len(old), len(new)], changed=len(ch), real=len(real),
               files=len(set(c["file"] for c in ch)), summary=summary,
               stale=[dict(key=s_["key"], at=s_["e"].get("at", ""), note=s_["note"],
                           cls=sorted({c["cls"] for c in s_["items"]})) for s_ in stale],
               review=[dict(key=r["key"], at=r["e"].get("at", ""), s=r["e"].get("key", ""),
                            cls=sorted({c["cls"] for c in r["items"]})) for r in review],
               units=units, services=services, open=open_items)
    json.dump(out, open(a.json, "w"), indent=1)


def _count(cs, k):
    d = defaultdict(int)
    for c in cs:
        d[c[k]] += 1
    return d


def gen_section(go, gn, old_tag, new_tag):
    """What re-running the translator (tools/tfex/translate.sh) changes in tfex/gen/ between the two tags."""
    out = [f"## 9. Generated code (tools/tfex/translate.sh: tfex/gen/ at {old_tag} vs {new_tag})\n"]
    if not os.path.isdir(go) or not os.path.isdir(gn):
        return out[0] + "Translator output missing (see .cache/tfex/gen-*.log).\n"
    files = sorted(set(os.listdir(go)) | set(os.listdir(gn)))
    out.append("Applying this update to the generated part = `tools/tfex/translate.sh all " + new_tag + "` and rebuilding. Per file:\n")
    for f in files:
        a = open(os.path.join(go, f), encoding="utf-8").read().split("\n") if os.path.exists(os.path.join(go, f)) else []
        b = open(os.path.join(gn, f), encoding="utf-8").read().split("\n") if os.path.exists(os.path.join(gn, f)) else []
        d = [l for l in difflib.unified_diff(a, b, lineterm="", n=1) if not l.startswith(("---", "+++"))]
        banner = lambda l: "GENERATED by" in l or l[1:].startswith((" * Sources", " * From "))
        plus = sum(1 for l in d if l.startswith("+") and not banner(l))
        minus = sum(1 for l in d if l.startswith("-") and not banner(l))
        if not plus and not minus:
            out.append(f"- `{f}`: unchanged"); continue
        out.append(f"- `{f}`: +{plus} −{minus}")
        if f.endswith(".hpp") and len(d) < 200:
            out.append("  <details><summary>diff</summary>\n\n```diff\n" + "\n".join(d) + "\n```\n  </details>")
    return "\n".join(out) + "\n"


def expect_section(path, ch):
    """Compares the labels with a hand reading (expect/*.toml: [[item]] match = "<file glob> <member glob>", class, what)."""
    ex = tomllib.load(open(path, "rb"))["item"]
    out = ["## 8. Check against the hand reading (" + os.path.basename(path) + ")\n",
           "| expected | what | found | verdict |", "|---|---|---|---|"]
    covered = set()
    ok = 0
    for e in ex:
        hits = [c for c in ch if (c["status"] == "syntax") == (e["class"] == "E") and match(c["rec"], e["match"])]
        for c in hits:
            covered.add(id(c))
        good = [c for c in hits if e["class"] in [l[0] for l in c["labels"]]]
        verdict = "ok" if good else ("**miss: not found**" if not hits else "**miss: labelled " +
                                     "/".join(sorted({'+'.join(l[0] for l in c['labels']) for c in hits})) + "**")
        ok += bool(good)
        out.append(f"| {e['class']} | {md_escape(e['what'])} | {len(hits)} members | {verdict} |")
    out.append(f"\n{ok} of {len(ex)} expected items labelled as expected.\n")
    extra = [c for c in ch if c["status"] != "syntax" and id(c) not in covered and c["cls"] in "ABC?"]
    if extra:
        out.append(f"Labelled A/B/C/? but not in the hand reading ({len(extra)}): check for false alarms.\n")
        for c in extra:
            out.append(f"- {'+'.join(l[0] for l in c['labels'])} `{md_escape(full(c['rec']))}` — {c['file']}")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    main()

"""Upkeep of manifest.toml / overrides.toml against a TF.EX tag (README.md).
  manifest.py fill <tag> [glob]  writes each [port] and [replace] entry's hash = the upstream body hash at <tag> (after porting
                                 <tag>; with a glob, only the entries whose key matches it, e.g. "*MapScene.cs*")
  manifest.py check <tag>     keys that match no member at <tag> (typos, renamed upstream)
  manifest.py coverage <tag>  how much of TF.EX's network + TF.State.Patchs code the manifest and overrides account for
  manifest.py prune [<tag>]   deletes the [port] entries and [skip]s whose members are all translated (tfex/gen/tfexgen_members.txt at
                              <tag>, default the vendor/tfex commit HEAD records; gen/ must be at that tag): that code is TF.EX's own now, and its changes arrive by regeneration
Only entries whose key matches are touched; `fill` rewrites the hash inside the entry's one-line inline table."""
import fnmatch, os, re, subprocess, sys, tomllib
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import update as U

FILES = {"manifest": os.path.join(HERE, "manifest.toml"), "overrides": os.path.join(HERE, "overrides.toml")}
SCOPE = ("network/", "state/TF.State.Patchs/", "state/TF.State.Domain/Extensions/", "state/TF.State.Domain/Randomness/")


def split(tag):
    return U.load_split(subprocess.check_output([os.path.join(HERE, "split.sh"), tag], text=True).strip())


def keys():
    m = tomllib.load(open(FILES["manifest"], "rb"))
    o = tomllib.load(open(FILES["overrides"], "rb"))
    return m.get("port", {}), o.get("skip", {}), o.get("replace", {})


def fill(tag, only=None):
    recs = split(tag)
    n = 0
    for name in ("manifest", "overrides"):
        path = FILES[name]
        out, section = [], None
        for line in open(path, encoding="utf-8"):
            s = line.rstrip("\n")
            m = re.match(r"\s*\[(\w+)\]", s)
            if m:
                section = m.group(1)
            m = re.match(r'(\s*"([^"]+)"\s*=\s*\{)(.*)\}(\s*(#.*)?)$', s)
            if m and section in ("port", "replace") and (only is None or fnmatch.fnmatchcase(m.group(2), only)):
                h, _ = U.entry_hash(recs, m.group(2))
                body = re.sub(r',?\s*hash\s*=\s*"[^"]*"', "", m.group(3)).rstrip()
                if h:
                    body += f', hash = "{h}"'
                    n += 1
                s = f"{m.group(1)}{body} }}{m.group(4) or ''}"
            out.append(s)
        open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"fill {tag}: {n} hashes written")


def check(tag):
    recs = list(split(tag).values())
    port, skip, rep = keys()
    bad = 0
    for kind, d in (("port", port), ("skip", skip), ("replace", rep)):
        for k in d:
            if not any(U.match(r, k) for r in recs):
                print(f"{kind}: no member at {tag}: {k}")
                bad += 1
    print(f"check {tag}: {len(port)} port, {len(skip)} skip, {len(rep)} replace; {bad} keys match nothing")
    return bad


def coverage(tag):
    recs = [r for r in split(tag).values() if r["file"].startswith(SCOPE)]
    port, skip, rep = keys()
    cnt = defaultdict(lambda: [0, 0, 0, 0])  # file -> ported, replaced, skipped, none
    for r in recs:
        i = 0 if any(U.match(r, k) for k in port) else 1 if any(U.match(r, k) for k in rep) else \
            2 if any(U.match(r, k) for k in skip) else 3
        cnt[r["file"]][i] += 1
    tot = [sum(c[i] for c in cnt.values()) for i in range(4)]
    print(f"coverage {tag} ({', '.join(SCOPE)}): {len(recs)} members: ported {tot[0]}, replaced {tot[1]}, skipped {tot[2]}, "
          f"unaccounted {tot[3]}")
    print("files with unaccounted members (unaccounted/total):")
    for f, c in sorted(cnt.items(), key=lambda kv: -kv[1][3]):
        if c[3]:
            print(f"  {c[3]:4}/{sum(c):<4} {f}")


def prune(pin=None):
    repo = os.path.dirname(os.path.dirname(HERE))
    pin = pin or subprocess.check_output(["git", "-C", repo, "ls-tree", "HEAD", "vendor/tfex"], text=True).split()[2]
    recs = list(split(pin).values())
    gm = U.load_members(os.path.join(repo, "hw-probe/exlaunch/source/program/tfex/gen"))
    if not gm:
        sys.exit("prune: no tfex/gen/tfexgen_members.txt (run tools/tfex/translate.sh all first)")
    gone = {}
    for name, section in (("manifest", "port"), ("overrides", "skip")):
        d = tomllib.load(open(FILES[name], "rb")).get(section, {})
        for k in d:
            hit = [r for r in recs if U.match(r, k)]
            if hit and all(U.generated(r, gm) for r in hit):
                gone[(name, k)] = len(hit)
    for name, section in (("manifest", "port"), ("overrides", "skip")):
        out, sec, n = [], None, 0
        for line in open(FILES[name], encoding="utf-8"):
            m = re.match(r"\s*\[(\w+)\]", line)
            if m:
                sec = m.group(1)
            m = re.match(r'\s*"([^"]+)"\s*=', line)
            if m and sec == section and (name, m.group(1)) in gone:
                n += 1
                continue
            out.append(line)
        open(FILES[name], "w", encoding="utf-8").write("".join(out))
        print(f"prune: {n} {section} entries deleted from {name}.toml (all their members are translated at {pin})")


if __name__ == "__main__":
    if sys.argv[1:2] == ["prune"] and len(sys.argv) <= 3:
        sys.exit(prune(*sys.argv[2:]))
    if len(sys.argv) not in (3, 4) or sys.argv[1] not in ("fill", "check", "coverage") or (len(sys.argv) == 4 and sys.argv[1] != "fill"):
        sys.exit(__doc__)
    r = {"fill": fill, "check": check, "coverage": coverage}[sys.argv[1]](*sys.argv[2:])
    sys.exit(1 if r else 0)

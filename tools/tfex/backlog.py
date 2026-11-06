#!/usr/bin/env python3
"""backlog.py: what of TF.EX runs on the Switch, and the queue for the rest (docs/STRATEGY-2026-10-28.md section 1).

Every TF.EX member at the vendor pin (the splitter's list: split.sh) gets one disposition, first match wins:
  generated  the translator emits it (gen/tfexgen_members.txt)
  replaced   a Switch replacement with its reason (overrides.toml [replace] / [service], translate.toml [[replace]] / [fortrise] absent)
  hand       a hand port of it (manifest.toml [port]): upkeep work at every release, to be replaced by generated code
  skipped    deliberately not on the Switch (overrides.toml [skip], [scope.exclude]), with its reason
  patch      missing, and its file is a Harmony patch class with no unit: a few lines of translate.toml, never a step's work (--check
             fails; 2026-10-28: three v0.27 patches sat in step 6 for a week)
  step N     missing, queued for that step of the strategy (backlog.toml [assign]; it wins over a skip the step will lift)
  unassigned missing and in no step: the classification pass isn't done (--check fails)
Lines are the members' source lines (methods, properties, constructors, operators, indexers; fields, type headers and interface
members are not counted). The protocol models' auto-properties count as generated when gen/tfexgen_Models.hpp has their struct.

backlog.py [--tag <pin>] [--out docs/TFEX-BACKLOG.md] [--check]
"""
import argparse, fnmatch, os, re, subprocess, sys, tomllib
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from update import load_split, match, load_members, generated  # noqa: E402

KINDS = {"method", "ctor", "property", "operator", "indexer"}
GEN = os.path.join(REPO, "hw-probe/exlaunch/source/program/tfex/gen")


def path_glob(rec_file, g):
    """[scope.exclude] globs: a trailing / is the whole folder; * within a segment, ** across."""
    if g.endswith("/"):
        return rec_file.startswith(g)
    return fnmatch.fnmatchcase(rec_file, g.replace("**", "*"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="HEAD", help="TF.EX commit or tag (default: the vendor/tfex checkout)")
    ap.add_argument("--out", default=os.path.join(REPO, "docs/TFEX-BACKLOG.md"))
    ap.add_argument("--check", action="store_true", help="exit 1 when a missing member is in no step")
    ap.add_argument("--list", metavar="DISP", help="print that disposition's members (e.g. unassigned, 'step 3') instead of writing the doc")
    a = ap.parse_args()

    sha = subprocess.check_output(["git", "-C", os.path.join(REPO, "vendor/tfex"), "rev-parse", a.tag + "^{commit}"], text=True).strip()
    split = subprocess.check_output([os.path.join(HERE, "split.sh"), sha], text=True).strip().splitlines()[-1]
    src = os.path.join(os.path.dirname(split), os.path.basename(split).replace("split-", "src-").removesuffix(".jsonl"), "src")
    harmony = {}
    def is_patch(f):
        if f not in harmony:
            try: harmony[f] = "[HarmonyPatch" in open(os.path.join(src, f), encoding="utf-8-sig").read()
            except OSError: harmony[f] = False
        return harmony[f]
    allrecs = list(load_split(split).values())
    ifaces = {(r["file"], r["type"]) for r in allrecs if r["kind"] == "interface"}
    recs = [r for r in allrecs if r["kind"] in KINDS and (r["file"], r["type"]) not in ifaces]   # an interface's members have no body
    gm = load_members(GEN)
    # the protocol models are emitted as structs (gen/tfexgen_Models.hpp, the translator's model pass): their auto-properties and
    # constructors are data, generated without a tfexgen_members.txt line
    structs = set(re.findall(r"\bstruct (\w+)\b", open(os.path.join(GEN, "tfexgen_Models.hpp")).read()))
    # and TF.EX's value structs the translator emits (gen/tfexgen_structs.hpp: Input, Vector2f), named by their comment
    structs |= {t.rpartition(".")[2] for t in re.findall(r"^struct \w+ \{   /\* ([\w.]+) \(", open(os.path.join(GEN, "tfexgen_structs.hpp")).read(), re.M)}
    # (and TF.State's value structs in tfexgen_structs.hpp, e.g. DeterministicRandom.State: their constructors are data too)
    state_structs = set(re.findall(r"^struct \w+ \{   /\* ([\w.]+) \(", open(os.path.join(GEN, "tfexgen_structs.hpp")).read(), re.M))
    def model_data(r):
        if r["kind"] == "ctor" and any(st.endswith("." + r["type"]) for st in state_structs):
            return True
        return r["file"].startswith("network/TF.EX.Domain/Models/") and r["type"].split(".")[-1].split("<")[0] in structs and \
            (r["kind"] == "ctor" or (r["kind"] == "property" and "get;" in r["text"].replace(" ", "")))
    manifest = tomllib.load(open(os.path.join(HERE, "manifest.toml"), "rb")).get("port", {})
    ov = tomllib.load(open(os.path.join(HERE, "overrides.toml"), "rb"))
    skips, replaces = ov.get("skip", {}), ov.get("replace", {})
    excludes = ov.get("scope", {}).get("exclude", {})
    tr_rep = {}
    for r in tomllib.load(open(os.path.join(HERE, "translate.toml"), "rb")).get("replace", []):
        t, _, mn = r["member"].rpartition(".")
        tr_rep[(t.rpartition(".")[2], mn)] = r
    # member rules by .NET name ("Namespace.Type.Member", "Type.*"): overrides.toml [service] (the Switch's code stands in for the member;
    # key D: not ours; key todo: still missing) and translate.toml [fortrise] absent (the member doesn't exist on the Switch, with its reason)
    named = {}
    for k, e in ov.get("service", {}).items():
        if isinstance(e, dict): named[k] = e
    for k, e in tomllib.load(open(os.path.join(HERE, "translate.toml"), "rb")).get("fortrise", {}).items():
        if isinstance(e, dict) and e.get("absent"): named[k] = e
    def named_rule(r):
        t = r["type"].split(".")[-1].split("<")[0]; mn = r["member"].split("(")[0].split("<")[0]
        for k, e in named.items():
            kt, _, km = k.rpartition(".")
            # a TF.State / TF.Replay / TF.EX rule only for that project's file (each has a ServiceCollections); a member name or a glob ("Log*")
            proj = next((d for p, d in (("TF.State.", "state/"), ("TF.Replay.", "replay/"), ("TF.EX.", "network/")) if k.startswith(p)), None)
            if proj and not r["file"].startswith(proj):
                continue
            if kt.rpartition(".")[2] == t and fnmatch.fnmatchcase(mn, km):
                return e
        return None
    bl = tomllib.load(open(os.path.join(HERE, "backlog.toml"), "rb"))
    steps, assign = bl["steps"], bl["assign"]

    def disp(r):
        if generated(r, gm) or model_data(r):
            return "generated", ""
        if (r["type"].split(".")[-1].split("<")[0], r["member"].split("(")[0].split("<")[0]) in tr_rep:
            return "replaced", "translate.toml [[replace]]"
        for k, e in replaces.items():
            if match(r, k):
                return "replaced", e.get("key", "") if isinstance(e, dict) else str(e)
        for k in manifest:
            if match(r, k):
                return "hand", manifest[k].get("at", "")
        if (e := named_rule(r)) is not None and e.get("key") not in ("todo", "D"):
            return "replaced", f'{e.get("key", "")}: {e.get("why", "")}'.strip(": ")
        if is_patch(r["file"]) and not any(match(r, k) for k in skips) and \
                not any(path_glob(r["file"], g) for g in excludes) and not any(match(r, k) for k, n in assign.items() if n == 8):
            return "patch", "a Harmony patch with no unit: give it one (translate.toml [[unit]] hooks = true) or a skip"
        for k, n in assign.items():   # before the skips: a skip a step will lift (the step deletes it when it ports the member)
            if match(r, k):
                return f"step {n}", ""
        if (e := named_rule(r)) is not None and e.get("key") == "D":
            return "skipped", "D: " + e.get("why", "")
        for k, why in skips.items():
            if match(r, k):
                return "skipped", why
        for g, why in excludes.items():
            if path_glob(r["file"], g):
                return "skipped", why
        return "unassigned", ""

    lines = lambda r: r["end"] - r["line"] + 1
    by = defaultdict(list)
    for r in recs:
        d, why = disp(r)
        by[d].append((r, why))
    total = sum(lines(r) for r in recs)
    if a.list:
        for r, why in by.get(a.list, []):
            print(f"{lines(r):4d} {r['file']} {r['type']}.{r['member'].split('(')[0]}" + (f"  ({why})" if why else ""))
        return
    order = ["generated", "replaced", "hand", "skipped", "patch"] + [f"step {n}" for n in sorted(steps, key=int)] + ["unassigned"]

    out = [f"# TF.EX backlog on the Switch", "",
           f"GENERATED by `tools/tfex/backlog.py` at TF.EX `{sha[:7]}`; do not edit. The queue's steps and their member globs are",
           "tools/tfex/backlog.toml; skips and replacements are overrides.toml / translate.toml, hand ports manifest.toml. The order is",
           "docs/STRATEGY-2026-10-28.md section 2.", "",
           "| disposition | members | lines | share |", "|---|---|---|---|"]
    for d in order:
        rs = by.get(d, [])
        name = d if not d.startswith("step ") else f"{d}: {steps[d[5:]]}"
        out.append(f"| {name} | {len(rs)} | {sum(lines(r) for r, _ in rs)} | {100 * sum(lines(r) for r, _ in rs) / total:.1f}% |")
    out.append(f"| **all** | **{len(recs)}** | **{total}** | |")

    def by_type(rs):
        t = defaultdict(lambda: [0, 0, ""])
        for r, why in rs:
            e = t[(r["file"], r["type"])]
            e[0] += 1; e[1] += lines(r); e[2] = e[2] or why
        return sorted(t.items(), key=lambda kv: -kv[1][1])

    for d in order[2:]:
        rs = by.get(d, [])
        if not rs:
            continue
        name = d if not d.startswith("step ") else f"{d}: {steps[d[5:]]}"
        out += ["", f"## {name} ({len(rs)} members, {sum(lines(r) for r, _ in rs)} lines)", "",
                "| file | type | members | lines |" + (" where / why |" if d in ("hand", "skipped") else ""),
                "|---|---|---|---|" + ("---|" if d in ("hand", "skipped") else "")]
        for (f, t), (n, ln, why) in by_type(rs):
            out.append(f"| {f} | {t} | {n} | {ln} |" + (f" {why.replace('|', '/')} |" if d in ("hand", "skipped") else ""))
    open(a.out, "w").write("\n".join(out) + "\n")
    un = by.get("unassigned", [])
    print(f"backlog: {len(recs)} members, {total} lines at {sha[:7]}: " +
          ", ".join(f"{d} {sum(lines(r) for r, _ in by.get(d, []))}" for d in order if by.get(d)) + f" -> {os.path.relpath(a.out, REPO)}")
    pt = by.get("patch", [])
    if pt:
        print("backlog: Harmony patches with no unit: " + ", ".join(sorted({r["type"] for r, _ in pt})), file=sys.stderr)
    if a.check and (un or pt):
        if un: print(f"backlog: {len(un)} missing members in no step (tools/tfex/backlog.toml)", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

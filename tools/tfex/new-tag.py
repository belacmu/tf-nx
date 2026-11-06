"""The one-page summary of a TF.EX update (tools/tfex/new-tag.sh): what changed, whether the simulation changed, coverage before and
after, the generated code, the build, and every hand-written / ledger / service / unit place of ours the change touches (for the
people who port by hand). Reads update.py's --json and the files new-tag.sh collects."""
import argparse, json, os, re

CLASS_NAME = {"A": "lockstep", "B": "protocol", "C": "UI", "D": "not ours", "E": "noise", "?": "no rule"}


def coverage(path):
    if not path or not os.path.exists(path):
        return None
    t = open(path, encoding="utf-8").read()
    m = re.search(r"\*\*(\d+) of (\d+) applicable methods.*?All methods, the ledger's skips included: (\d+) of (\d+)", t, re.S)
    return f"{m[1]}/{m[2]} applicable ({m[3]}/{m[4]} all)" if m else None


def main():
    ap = argparse.ArgumentParser()
    for k in ("json", "old", "new", "old-sha", "new-sha", "commits", "cov-old", "cov-new", "gen-diff", "manifest-check", "build"):
        ap.add_argument("--" + k)
    a = ap.parse_args()
    d = json.load(open(a.json))
    s = d["summary"]
    W = []
    w = W.append
    tot = lambda k, ds=("stale", "override", "new", "unported"): sum(s.get(k, {}).get(x, 0) for x in ds)

    fo, fn = a.old.replace("/", "_"), a.new.replace("/", "_")
    w(f"# TF.EX {a.old} → {a.new}: update summary\n")
    w(f"`tools/tfex/new-tag.sh {a.new} {a.old}`: vendor/tfex {a.old_sha} → {a.new_sha}, {a.commits} commits; "
      f"{d['real']} changed members in {d['files']} files (members {d['members'][0]} → {d['members'][1]}). "
      f"Full report: [{fo}-{fn}.md]({fo}-{fn}.md).\n")

    w("## Verdict\n")
    na = sum(s.get("A", {}).values())
    sa = [e for e in d["stale"] if "A" in e["cls"]]
    oa = [o for o in d["open"] if o["cls"] == "A"]
    w(f"- **Simulation (A, lockstep): {na} changed members; the port is behind on {len(sa)} (manifest), {len(oa)} have no ledger entry.** "
      + ("Nothing to port for lockstep." if not sa and not oa else
         "Read them (report sections 1 and 3: a renamed Harmony patch, Postfix to Finalizer, shows as removed + added): a real body "
         "change must be ported before a Switch plays this version, then a Switch-vs-TF.EX match on it "
         "(`tools/smoke.sh lobby`, browser on the same build) and the rollback drill."))
    nb = tot("B")
    w(f"- Protocol (B): {'no change' if not nb else f'{nb} changes (server messages, models: the report section 7)'}")
    w(f"- UI (C): {tot('C')}; no rule (?): {tot('?')} to triage; not ours (D): {tot('D', ('stale','override','new','unported','skipped'))}")
    w(f"- Coverage: {coverage(a.cov_old) or '?'} → **{coverage(a.cov_new) or '?'}**")
    gd = open(a.gen_diff).read().strip() if a.gen_diff and os.path.exists(a.gen_diff) else ""
    files = [l for l in gd.splitlines() if "|" in l]
    w(f"- Generated code (tfex/gen, binding.json): {len(files)} files changed" + (": " + ", ".join(
        "`" + os.path.basename(l.split("|")[0].strip()) + "`" for l in files[:12]) + (" …" if len(files) > 12 else "") if files else ""))
    w(f"- Module build: **{a.build}**")
    mc = open(a.manifest_check).read().strip().splitlines() if a.manifest_check and os.path.exists(a.manifest_check) else []
    dead = [l for l in mc if l.startswith(("port:", "skip:", "replace:"))]
    w(f"- Ledger keys that match nothing at {a.new}: {len(dead)}" + ("".join(f"\n  - {l}" for l in dead[:8])))
    w("")

    w("## Our places this touches (for the hand port)\n")
    if d["stale"]:
        w("**Hand-written ports behind the new body** (manifest.toml; refill its hash with `manifest.py fill` once ported):\n")
        for e in d["stale"]:
            w(f"- [{'+'.join(e['cls']) or '-'}] `{e['key']}` → {e['at'] or '?'}" + (f" ({e['note']})" if e["note"] else ""))
        w("")
    if d["review"]:
        w("**Ledger replacements to re-check** (overrides.toml [replace]):\n")
        for e in d["review"]:
            w(f"- [{'+'.join(e['cls']) or '-'}] `{e['key']}` ({e['s']}) → {e['at'] or '?'}")
        w("")
    if d["units"]:
        w("**Translated units** (translate.toml; regenerated above, check the diff and run the flow they serve):\n")
        for u in d["units"]:
            w(f"- {u['unit']}: " + ", ".join(f"`{m}`" for m in u["members"]))
        w("")
    if d["services"]:
        w("**Service map** (overrides.toml [service]: the hand-written Switch side may need the new behaviour):\n")
        for e in d["services"]:
            w(f"- `{e['key']}`: " + ", ".join(f"`{m}`" for m in e["members"]))
        w("")
    op = [o for o in d["open"] if o["cls"] in ("A", "B")]
    rest = [o for o in d["open"] if o["cls"] not in ("A", "B")]
    if op or rest:
        w(f"**New or unported, no ledger entry** ({len(op)} A/B, {len(rest)} C/?): port, or add a skip with its reason:\n")
        for o in op + rest[:12]:
            w(f"- [{o['cls']}] {o['status']} `{o['member']}` ({o['file']})" + (" → near " + "; ".join(o["near"]) if o["near"] else ""))
        if len(rest) > 12:
            w(f"- … {len(rest) - 12} more C/? items in the full report, section 3")
        w("")
    if not any((d["stale"], d["review"], d["units"], d["services"], d["open"])):
        w("Nothing of ours is touched.\n")

    w("## Next\n")
    w("1. Read the verdict; port A first (lockstep), then B, then the places above. Ask tfex-adopt for the hand-written ones.")
    w(f"2. After porting: `python3 tools/tfex/manifest.py fill {a.new} [key glob]`, then `manifest.py check {a.new}`.")
    w("3. Commit vendor/tfex, tools/tfex/reports, binding.json and tfex/gen together; a lobby match (smoke test) before merging.")
    print("\n".join(W))


if __name__ == "__main__":
    main()

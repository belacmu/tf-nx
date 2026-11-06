#!/usr/bin/env python3
"""check.py [--json]: is anything TF.NX carries behind its upstream? (docs/PLAN-TFNX-2026-11-04.md 5b: a TF.NX release follows every TF.EX
release and every catalog mod's update.) One table; exit 0 when everything is current, 2 when something is newer, 1 when a source failed.

What TF.NX carries: TF.EX's and FortRise's versions from the committed generated code (tfexgen_version.hpp, tfexgen_mods_builtin.hpp),
each catalog mod's from mods/<name>/meta.json. Where the newest is announced:
  - TF.EX: GitHub releases of Fcornaire/TF.EX (the official server only lets the latest version play online: first in the table);
  - FortRise: GitHub releases of FortRise/FortRise (prereleases too: TF.NX follows FortRise 5.5's betas);
  - a mod: mods/<name>/mod.toml [update] (github = "owner/repo" | gamebanana = <id> | local = "<a meta.json, found in a folder above the
    checkout>": our own mods' repositories), else its meta.json's
    own "update": {"gamebanana": {"id": ...}} (FortRise's convention). A GameBanana page without a version is compared by its last
    update's date with mod.toml [update] seen = "<YYYY-MM-DD>" (raise it after looking).
Uses `gh` for GitHub (its login: no rate limit) and GameBanana's public API."""
import datetime, json, os, re, subprocess, sys, tomllib, urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GEN = os.path.join(ROOT, "hw-probe/exlaunch/source/program/tfex/gen")


def ver_key(v):
    """FortRise's SemanticVersion order (runtime/tfexgen_mods.cpp CompareVersions): numbers, a pre-release before its release"""
    v = v.lstrip("vV")
    main, _, pre = v.partition("-")
    nums = [int(x) if x.isdigit() else 0 for x in re.split(r"[.+]", main)[:4]] + [0] * 4
    parts = [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")] if pre else []
    return (nums[:4], 0 if pre else 1, parts)


def gh_latest(repo, pre=False):
    if pre:
        out = subprocess.run(["gh", "api", f"repos/{repo}/releases", "--jq", ".[0] | [.tag_name, .published_at] | @tsv"], capture_output=True, text=True)
    else:
        out = subprocess.run(["gh", "api", f"repos/{repo}/releases/latest", "--jq", "[.tag_name, .published_at] | @tsv"], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip(): raise RuntimeError(f"GitHub {repo}: {out.stderr.strip()[:120] or 'no release'}")
    tag, when = out.stdout.strip().split("\t")
    return tag.lstrip("vV"), when[:10]


def gamebanana(mid):
    url = f"https://gamebanana.com/apiv11/Mod/{int(mid)}?_csvProperties=_sName,_sVersion,_tsDateUpdated"
    req = urllib.request.Request(url, headers={"User-Agent": "TF.NX-update-check"})
    d = json.load(urllib.request.urlopen(req, timeout=20))
    when = datetime.datetime.fromtimestamp(d.get("_tsDateUpdated", 0), datetime.timezone.utc).strftime("%Y-%m-%d")
    return (d.get("_sVersion") or "").strip(), when


def find_up(rel):
    """a path next to the checkout or above it (our own mods' repositories sit beside switch-online; a worktree is deeper)"""
    d = ROOT
    while True:
        if os.path.exists(os.path.join(d, rel)): return os.path.join(d, rel)
        if os.path.dirname(d) == d: return rel
        d = os.path.dirname(d)


def carried():
    tfex = re.search(r'TFEX_VERSION "([^"]+)"', open(os.path.join(GEN, "tfexgen_version.hpp")).read()).group(1)
    fr = re.search(r'"name":"FortRise","version":"([^"]+)"', open(os.path.join(GEN, "tfexgen_mods_builtin.hpp")).read()).group(1)
    rows = [("TF.EX", tfex, ("github", "Fcornaire/TF.EX", False), None), ("FortRise", fr, ("github", "FortRise/FortRise", True), None)]
    for d in sorted(os.listdir(os.path.join(ROOT, "mods"))):
        p = os.path.join(ROOT, "mods", d, "mod.toml")
        if not os.path.isfile(p): continue
        t = tomllib.load(open(p, "rb"))
        meta = {k.lower(): v for k, v in json.load(open(os.path.join(ROOT, "mods", d, t["meta"]), encoding="utf-8-sig")).items()}
        u = t.get("update", {})
        if "github" in u: src = ("github", u["github"], bool(u.get("prerelease")))
        elif "gamebanana" in u: src = ("gamebanana", u["gamebanana"], None)
        elif "local" in u: src = ("local", find_up(u["local"]), None)
        elif meta.get("update", {}).get("gamebanana", {}).get("id"): src = ("gamebanana", meta["update"]["gamebanana"]["id"], None)
        else: src = None
        rows.append((meta.get("name", d), meta.get("version", "?"), src, u.get("seen")))
    return rows


def main():
    results, worst = [], 0
    for name, have, src, seen in carried():
        latest, when, status, where = "", "", "", ""
        try:
            if src is None: status, where = "no update source (mod.toml [update])", "-"
            elif src[0] == "github": latest, when = gh_latest(src[1], src[2]); where = f"github {src[1]}"
            elif src[0] == "gamebanana": latest, when = gamebanana(src[1]); where = f"gamebanana {src[1]}"
            elif src[0] == "local" and not os.path.isfile(src[1]):
                status, where = "skipped (its repository isn't on this machine)", "local " + src[1]
            elif src[0] == "local":
                latest = {k.lower(): v for k, v in json.load(open(src[1], encoding="utf-8-sig")).items()}.get("version", ""); where = "local " + os.path.relpath(src[1], ROOT)
            if not status:
                if latest: newer = ver_key(latest) > ver_key(have); status = f"NEWER: {latest}" if newer else "current"
                elif seen: newer = when > seen; status = f"CHANGED {when} (seen {seen})" if newer else f"current (no version; updated {when})"
                else: newer = True; status = f"no version upstream (updated {when}): set [update] seen"
                worst = max(worst, 2 if newer else 0)
        except Exception as e:
            status, worst = f"ERROR: {e}", max(worst, 1)
        results.append({"name": name, "carried": have, "latest": latest, "updated": when, "status": status, "source": where})
    if "--json" in sys.argv: print(json.dumps(results, indent=1))
    else:
        w = max(len(r["name"]) for r in results)
        print(f"{'component':<{w}}  {'TF.NX carries':<16} {'status':<44} source")
        for r in results: print(f"{r['name']:<{w}}  {r['carried']:<16} {r['status']:<44} {r['source']}")
    sys.exit(1 if worst == 1 else worst)


main()

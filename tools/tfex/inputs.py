#!/usr/bin/env python3
"""inputs.py check <tag>: the translator's inputs from outside the repo (evaluation rec. 2b). Fails (exit 1) when
 - the FortRise-patched game (TowerFall.Patch.dll, translate.toml [refs] files) is missing: without it Roslyn binds FortRise's
   members as invalid and the output differs (TFEX_ALLOW_VANILLA=1 to translate against the vanilla exe on purpose);
 - vendor/tfex is checked out at neither the commit HEAD records nor <tag> (a `git commit -a` would move the pointer; fix:
   git submodule update vendor/tfex).
Then records each outside input's sha256 in tools/tfex/reports/inputs-<tag>.sha256 (committed with the reports: a changed input
shows as a diff there)."""
import hashlib, os, subprocess, sys, tomllib
from pathlib import Path

here = Path(__file__).resolve().parent; repo = here.parent.parent

def expand(p): return os.path.expanduser(os.path.expandvars(p))
def git(*a, cwd=repo): return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True).stdout.strip()
def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

def main():
    if len(sys.argv) != 3 or sys.argv[1] != "check": print(__doc__, file=sys.stderr); return 2
    tag = sys.argv[2]
    refs = tomllib.loads((here / "translate.toml").read_text())["refs"]
    errors = []
    files = [expand(f) for f in refs.get("files", []) if expand(f)]
    if not any(Path(f).name == "TowerFall.Patch.dll" and Path(f).is_file() for f in files) and os.environ.get("TFEX_ALLOW_VANILLA") != "1":
        want = next((f for f in files if Path(f).name == "TowerFall.Patch.dll"), "TowerFall.Patch.dll")
        errors.append(f"the FortRise-patched game is missing: {want} (the browser's tools/FortRisePatch makes it; "
                      "TFEX_ALLOW_VANILLA=1 to translate against the vanilla exe)")
    ptr = git("ls-tree", "HEAD", "vendor/tfex").split()[2] if git("ls-tree", "HEAD", "vendor/tfex") else ""
    co = git("rev-parse", "HEAD", cwd=repo / "vendor/tfex")
    tagc = git("rev-parse", f"{tag}^{{commit}}", cwd=repo / "vendor/tfex")
    if ptr and co not in (ptr, tagc):
        errors.append(f"vendor/tfex is checked out at {co[:7]}, but HEAD records {ptr[:7]} (and {tag} is {tagc[:7]}): "
                      "git submodule update vendor/tfex (or new-tag.sh moves it)")
    for e in errors: print("inputs: " + e, file=sys.stderr)
    if errors: return 1
    lines = []
    for f in files + [expand(m) for m in refs.get("mono", [])]:
        if Path(f).is_file(): lines.append(f"{sha(f)}  {f.replace(str(Path.home()), '~')}")
    for d in refs.get("dirs", []):
        for f in sorted(Path(expand(d)).glob("*.dll")) if Path(expand(d)).is_dir() else []:
            lines.append(f"{sha(f)}  {str(f).replace(str(Path.home()), '~')}")
    out = here / "reports" / f"inputs-{tag.replace('/', '_')}.sha256"
    out.parent.mkdir(exist_ok=True)
    text = f"# translator inputs from outside the repo at {tag} (tools/tfex/inputs.py; translate.toml [refs])\n" + "\n".join(lines) + "\n"
    if not out.exists() or out.read_text() != text:
        if out.exists(): print(f"inputs: the outside inputs changed since {out.name} was recorded (see git diff)", file=sys.stderr)
        out.write_text(text)
    return 0

sys.exit(main())

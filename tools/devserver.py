#!/usr/bin/env python3
"""devserver.py [--port 8765] [--list tools/devserve.list]: dev updates for the Switch over Wi-Fi (hw-probe devupdate.cpp).

The console polls GET /manifest.txt (computed on every request from the list, so a fresh build is what it gets) and downloads the files
whose size/CRC differ (GET /f/<n>); test logs come back as POST /upload/<name>, saved under .devserve/results/.

The list (default tools/devserve.list): one `<sd path> <local path>` per line, local paths relative to switch-online/; a missing local
file is skipped; an sd path ending in / maps a whole local folder. On the console: sd:/xplay/devserver.txt = "<this Mac's IP> <port>".
"""
import argparse, http.server, os, sys, time, zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # switch-online/
RESULTS = os.path.join(HERE, ".devserve", "results")

def entries(list_path):
    out = []
    for line in open(list_path):
        line = line.split("#", 1)[0].strip()
        if not line: continue
        sd, local = line.split(None, 1)
        p = local if os.path.isabs(local) else os.path.join(HERE, local)
        if sd == "delete":   # `delete <sd path>`: the console removes that file or folder if it's there (manifest2 only)
            out.append(("delete", local)); continue
        if sd.endswith("/") and os.path.isdir(p):   # a folder: every file under it, same relative paths (no macOS ._ files)
            for root, dirs, files in sorted(os.walk(p)):
                dirs.sort()
                for fn in sorted(files):
                    if fn.startswith("._") or fn == ".DS_Store": continue
                    full = os.path.join(root, fn)
                    out.append((sd + os.path.relpath(full, p).replace(os.sep, "/"), full))
        elif os.path.isfile(p): out.append((sd, p))
    return out

def make_handler(list_path):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *a): pass
        def send(self, code, body, ctype="application/octet-stream"):
            self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close"); self.end_headers(); self.wfile.write(body)
        def do_GET(self):
            es = entries(list_path)
            if self.path in ("/manifest.txt", "/manifest2.txt"):
                # manifest2 (devupdate builds from 2026-10-16 on): sd paths with %20 for spaces. The old manifest leaves out paths with
                # spaces (older builds split lines on whitespace), so an old console can still update itself to a new build first.
                v2 = self.path == "/manifest2.txt"
                lines = []
                for i, (sd, p) in enumerate(es):
                    if sd == "delete":
                        if v2: lines.append(f"delete {p.replace(' ', '%20')}")
                        continue
                    if not v2 and (" " in sd or "/romfs/" in sd): continue   # old builds don't drop the romfs cache either
                    data = open(p, "rb").read()
                    lines.append(f"file {sd.replace(' ', '%20')} {len(data)} {zlib.crc32(data) & 0xffffffff:08x} /f/{i}")
                return self.send(200, ("\n".join(lines) + "\n").encode(), "text/plain")
            if self.path.startswith("/f/"):
                try: sd, p = es[int(self.path[3:])]
                except (ValueError, IndexError): return self.send(404, b"no such file")
                data = open(p, "rb").read()
                print(f"[dev] {time.strftime('%H:%M:%S')} {self.client_address[0]} downloads {sd} ({len(data)} bytes)", flush=True)
                return self.send(200, data)
            self.send(404, b"not found")
        def do_POST(self):
            if not self.path.startswith("/upload/"): return self.send(404, b"not found")
            name = os.path.basename(self.path[len("/upload/"):]) or "upload.txt"
            n = int(self.headers.get("Content-Length", "0"))
            data = self.rfile.read(n)
            os.makedirs(RESULTS, exist_ok=True)
            path = os.path.join(RESULTS, f"{time.strftime('%Y%m%d-%H%M%S')}-{name}")
            open(path, "wb").write(data)
            print(f"[dev] {time.strftime('%H:%M:%S')} {self.client_address[0]} uploaded {name} ({len(data)} bytes) -> {path}", flush=True)
            self.send(200, b"ok", "text/plain")
    return H

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    chan = os.path.join(HERE, ".devserve", "channel", "devserve.list")   # tools/devserve-publish.sh: a verified checkout's set
    ap.add_argument("--list", default=chan if os.path.isfile(chan) else os.path.join(HERE, "tools", "devserve.list"))
    a = ap.parse_args()
    for sd, p in entries(a.list): print(f"[dev] serving {sd} <- {os.path.relpath(p, HERE)}")
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", a.port), make_handler(a.list))
    print(f"[dev] listening on port {a.port}", flush=True)
    srv.serve_forever()

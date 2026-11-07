#!/usr/bin/env python3
"""server.py <port> <version> <zip>: GitHub's releases for TF.NX's updater, as the console sees them (tools/update-test.sh).
  HEAD|GET /releases/latest                              -> 302 /releases/tag/v<version>          (github.com's redirect)
  GET /releases/download/v<version>/TF.NX-v<version>.zip -> 302 /assets/<n>                         (to the asset host, as GitHub)
  GET /assets/<n>                                        -> the zip (Content-Length)
Every request is printed (method, path, status)."""
import http.server, os, sys

port, version, zpath = int(sys.argv[1]), sys.argv[2], sys.argv[3]
data = open(zpath, "rb").read()

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *a): print("server:", self.command, self.path, fmt % a if a else "", flush=True)
    def redirect(self, to):
        self.send_response(302); self.send_header("Location", to); self.send_header("Content-Length", "0"); self.end_headers()
    def route(self, body):
        p = self.path
        if p == "/releases/latest": return self.redirect(f"http://{self.headers['Host']}/releases/tag/v{version}")
        if p == f"/releases/download/v{version}/TF.NX-v{version}.zip": return self.redirect("/assets/1")
        if p == "/assets/1":
            self.send_response(200); self.send_header("Content-Type", "application/octet-stream"); self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if body: self.wfile.write(data)
            return
        self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()
    def do_HEAD(self): self.route(False)
    def do_GET(self): self.route(True)

http.server.ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()

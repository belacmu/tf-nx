#!/usr/bin/env python3
"""Line-echo TCP server for VIABILITY-TESTS 7a: echo-server.py [port] (default 9000).
Listens on all interfaces; prints each connection."""
import socketserver, sys

class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        print("connected:", self.client_address, flush=True)
        while True:
            data = self.request.recv(1024)
            if not data:
                break
            self.request.sendall(data)
        print("closed:", self.client_address, flush=True)

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True

port = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
print("echoing on port", port, flush=True)
Server(("0.0.0.0", port), Echo).serve_forever()

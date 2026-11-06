# Relay Worker (VIABILITY-TESTS 7c, real network)

`worker.js`: the WebSocket relay of `../tools/relay-server.mjs` as a Cloudflare Worker with one
Durable Object per room. **Deployed 2026-10-14** as `tf-relay-probe`:
`https://<relay-probe-worker>` (`wrangler.toml` has the Durable Object binding
and the SQLite migration, which the free plan includes). It's a public endpoint with small limits
(4 peers per room, 1 KB messages, 150 messages/s per connection). It's temporary: delete it when
the tests are done.

```
cd relay-worker
npx wrangler deploy      # (wrangler 4 is cached from the browser repo's deploys)
npx wrangler delete      # when finished
```

Test it with the Node twin of the Switch's test, from this machine:

```
# a peer in the room: a browser at https://<worker>/echo.html?room=relaytest, or
node ../tools/relay-peer.mjs wss://<relay-probe-worker> rt1
node ../tools/relay-probe.mjs wss://<relay-probe-worker> 300 rt1
# the Switch probe's room is always "relaytest"; put the browser tab (or the Node peer) in that room
```

# SEED PC matchmaking Worker

Cloudflare Workers + one SQLite-backed Durable Object coordinate casual Quick
Match. Game inputs and rollback snapshots continue directly between players.
This service is separate from the existing manual Host/Join codes.

## Protocol

- `GET /health`: service/version check; does not activate the Durable Object.
- `GET /v1/status`: public waiting and eligible counts, without names or invitations.
- `GET /v1/connect`: WebSocket endpoint. Send `hello` with protocol 1, the
  compatibility ID in `core.mjs`, a UTF-8 name of at most 48 bytes, and region
  `any`, `na`, `eu`, `as`, `oc`, `sa`, or `af`.
- `enqueue` enters the queue. Two compatible participants receive `match_found`
  with unique match ID and host/guest roles. Both send `accept` for that match.
- The host receives `prepare`, creates its usual native AutoPunch room, then sends
  `publish` with its SEED1 invitation. Only the assigned guest receives `connect`.
- Each participant sends `connected` after the native room handshake. `handoff`
  releases matchmaking state; the existing native room handles selection,
  rollback and unlimited rematches independently of this service.
- `cancel`, closed sockets and timeouts release reservations. Counts include
  waiting tickets, excluding offered matches, active players and observers.
- `ping` receives the automatic `pong` response without waking a sleeping object.

The service accepts both the original 0.1.4 compatibility ID and the fixed
build ID. Pairing and waiting counts are isolated by compatibility ID; old
clients can still play each other, while fixed clients receive only fixed
opponents. `GET /v1/status?build=<compatibility-id>` selects the matching
counter. Both players must update to play together with the restored intro.

Names and pending match metadata live in WebSocket attachments so hibernation
can reconstruct them. Invitation codes are forwarded only to the assigned peer;
they are never included in public counters or stored in a database. Routine
request logging is disabled. The client network stack still exposes each peer's
endpoint to its chosen opponent as part of the existing direct connection.

## Free-tier design

One shared coordinator avoids one continuously active object per room. Counter
broadcasts are batched every 200 ms at most. A 15-second alarm is scheduled only
while players are queued or have a pending connection. The initial service caps
connections at 1,024 overall and eight per source IP, limits message sizes to
4 KiB, and limits application commands per connection. These caps are resource
protection, not a validated player-capacity promise. No paid plan, custom domain,
game relay, database service or spectator stream is required for this Worker.

## Development and deployment

Requires Node.js 24 and the pinned Wrangler version in `package.json`.

```powershell
npm ci
node --test core.test.mjs
npx wrangler dev --local --ip 127.0.0.1 --port 8788 --persist-to <E-work-directory>
node integration.mjs http://127.0.0.1:8788
npx wrangler login --scopes account:read user:read workers_scripts:write
npx wrangler deploy
node integration.mjs https://<worker>.<account-subdomain>.workers.dev --observe-only
```

For this workspace, keep npm caches, Wrangler configuration, logs, temporary
files and local Durable Object state in `../../../../work/cloudflare`, on E:.
Wrangler credentials, `.dev.vars`, `.wrangler` state and `node_modules` must never
be committed or bundled in a game release. The migration explicitly creates a
SQLite-backed class, which is supported on the Workers Free plan.

The core tests cover pairing, duplicate tickets, regions, invitation isolation,
acceptance, disconnects, expiry, malformed messages, restored attachments and
500-client allocation, and legacy/fixed queue isolation. The integration script exercises the real Worker runtime
and WebSocket/hibernation APIs locally. Use `--observe-only` for a live
production smoke check that never enters the player queue.

Public lobby browsing and delayed spectating remain separate integrations.
The current native source implements the Quick Match tab, live counts and
region filtering. Previously downloaded 0.1.4-dev executables need the updated
native build; deploying a Worker cannot change a downloaded executable.

## Deployment status

The matchmaking backend is deployed at
`https://seedpc-matchmaking.lord19uga.workers.dev`. Its health check and live
WebSocket integration tests passed, including queue counts, unique roles,
private invitation handoff and disconnect/requeue handling. Native Windows clients also passed live count and real UDP-room handoff checks.
Two full US-menu instances paired through this backend, selected JP fighters
and stages, completed two synchronized matches in the same room and returned
to the menu. Private Host/Join remains independent of this service.

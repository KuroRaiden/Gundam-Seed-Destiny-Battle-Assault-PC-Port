// Shared lobby coordination only. No gameplay inputs, game snapshots or ROM data.
export const PROTOCOL = 1;
export const COMPATIBILITY = "seedpc-jp-r9-a8-20261027";
export const REGIONS = new Set(["any", "na", "eu", "as", "oc", "sa", "af"]);
const encoder = new TextEncoder();
export function playerName(value) {
  if (typeof value !== "string") throw new Error("invalid_name");
  const name = value.trim().replace(/[\u0000-\u001f\u007f]/g, "");
  if (!name || encoder.encode(name).length > 48) throw new Error("invalid_name");
  return name;
}
export function validCode(value) {
  return typeof value === "string" && /^SEED1-[A-Za-z0-9_-]{46}$/.test(value);
}
export class LobbyCore {
  constructor(peers, options = {}) {
    this.peers = peers;
    this.now = options.now ?? Date.now;
    this.uuid = options.uuid ?? (() => crypto.randomUUID());
    this.fresh = options.fresh ?? ((peer) => peer.data.lastSeen);
    this.notify = options.notify;
  }
  save(peer) { peer.socket.serializeAttachment(peer.data); }
  send(peer, value) {
    try { peer.socket.send(JSON.stringify({ protocol: PROTOCOL, ...value })); }
    catch { /* A disconnect never blocks another participant. */ }
  }
  all() { return this.peers().filter(p => p.data && p.data.phase !== "closed"); }
  waiting(region = "any") {
    return this.all().filter(p => p.data.build === COMPATIBILITY && p.data.phase === "queued" &&
      this.now() - this.fresh(p) <= 75000 &&
      (region === "any" || p.data.region === "any" || p.data.region === region));
  }
  counts(region = "any") {
    return { type: "count", waiting: this.waiting().length,
             eligible: this.waiting(region).length, region };
  }
  broadcastCounts() {
    if (this.notify) { this.notify(); return; }
    const peers = this.all();
    const queued = peers.filter(p => p.data.phase === "queued" && p.data.build === COMPATIBILITY &&
                                  this.now() - this.fresh(p) <= 75000);
    const eligible = new Map([...REGIONS].map(region => [region, queued.filter(p =>
      region === "any" || p.data.region === "any" || p.data.region === region).length]));
    for (const peer of peers) if (peer.data.build === COMPATIBILITY)
      this.send(peer, { type: "count", waiting: queued.length,
                        eligible: eligible.get(peer.data.region), region: peer.data.region });
  }
  initialize(socket, ip) {
    const peer = { socket, data: { id: this.uuid(), ip, phase: "new", name: "Player",
      build: "", region: "any", lastSeen: this.now(), since: 0,
      match: "", partner: "", role: "", accepted: false, deadline: 0,
      rateAt: this.now(), rateCount: 0 } };
    this.save(peer);
    this.send(peer, { type: "welcome", session: peer.data.id });
    return peer;
  }
  receive(peer, message) {
    if (typeof message !== "string" || encoder.encode(message).length > 4096)
      return this.reject(peer, "message_too_large", true);
    let event;
    try { event = JSON.parse(message); } catch { return this.reject(peer, "invalid_json"); }
    if (!event || Array.isArray(event) || typeof event !== "object" || typeof event.type !== "string")
      return this.reject(peer, "invalid_message");
    if (this.now() - peer.data.rateAt >= 10000) {
      peer.data.rateAt = this.now(); peer.data.rateCount = 0;
    }
    peer.data.lastSeen = this.now();
    if (++peer.data.rateCount > 30) return this.reject(peer, "rate_limited", true);
    this.save(peer);
    this.sweep();
    peer.data = peer.socket.deserializeAttachment();
    if (peer.data.phase === "closed") return;
    try {
      if (event.type === "hello") {
        if (!["new", "idle"].includes(peer.data.phase)) throw new Error("already_searching");
        if (event.protocol !== PROTOCOL || event.build !== COMPATIBILITY) throw new Error("incompatible_build");
        if (!REGIONS.has(event.region)) throw new Error("invalid_region");
        peer.data.name = playerName(event.name);
        peer.data.region = event.region;
        peer.data.build = event.build;
        peer.data.phase = "idle";
        this.save(peer);
        this.send(peer, { ...this.counts(peer.data.region), type: "ready" });
      } else if (event.type === "enqueue") {
        if (peer.data.build !== COMPATIBILITY) throw new Error("hello_required");
        if (peer.data.phase === "queued") { this.send(peer, { type: "searching" }); return; }
        if (peer.data.phase !== "idle") throw new Error("already_matched");
        peer.data.phase = "queued"; peer.data.since = this.now();
        this.save(peer); this.send(peer, { type: "searching" }); this.pair();
      } else if (event.type === "cancel") {
        this.release(peer, false); this.pair();
        this.send(peer, { type: "idle" });
      } else if (event.type === "accept") {
        this.requireMatch(peer, event, "offered");
        peer.data.accepted = true; this.save(peer);
        const other = this.partner(peer);
        if (other && other.data.accepted && other.data.phase === "offered") {
          const host = peer.data.role === "host" ? peer : other;
          const guest = peer.data.role === "guest" ? peer : other;
          for (const p of [host, guest]) {
            p.data.phase = "connecting"; p.data.deadline = this.now() + 75000; this.save(p);
          }
          this.send(host, { type: "prepare", match: host.data.match, role: "host" });
          this.send(guest, { type: "preparing", match: guest.data.match, role: "guest" });
        }
      } else if (event.type === "publish") {
        this.requireMatch(peer, event, "connecting");
        if (peer.data.role !== "host" || !validCode(event.code)) throw new Error("invalid_invitation");
        const other = this.partner(peer);
        if (!other || !["connecting", "playing"].includes(other.data.phase)) throw new Error("opponent_left");
        this.send(other, { type: "connect", match: peer.data.match, code: event.code,
                           opponent: peer.data.name });
        this.send(peer, { type: "published", match: peer.data.match });
      } else if (event.type === "connected") {
        if (event.match !== peer.data.match || !["connecting", "playing"].includes(peer.data.phase))
          throw new Error("stale_match");
        peer.data.phase = "playing"; peer.data.deadline = 0; this.save(peer);
        const other = this.partner(peer);
        if (other?.data.phase === "playing") {
          for (const p of [peer, other]) {
            this.send(p, { type: "handoff", match: p.data.match });
            this.clear(p, "idle");
          }
        }
      } else if (event.type === "count") {
        this.send(peer, this.counts(peer.data.region));
      } else throw new Error("unknown_message");
    } catch (error) { this.reject(peer, error.message); }
    this.broadcastCounts();
  }
  reject(peer, reason, close = false) {
    this.send(peer, { type: "error", reason });
    if (close) {
      this.closed(peer);
      try { peer.socket.close(1008, reason); } catch {}
    }
  }
  requireMatch(peer, event, phase) {
    if (!event.match || event.match !== peer.data.match || peer.data.phase !== phase)
      throw new Error("stale_match");
  }
  partner(peer) { return this.all().find(p => p.data.id === peer.data.partner && p.data.match === peer.data.match); }
  clear(peer, phase) {
    Object.assign(peer.data, { phase, match: "", partner: "", role: "", accepted: false, deadline: 0 });
    this.save(peer);
  }
  release(peer, closed) {
    const other = peer.data.match ? this.partner(peer) : null;
    if (other && other.data.phase !== "playing") {
      this.clear(other, "queued"); other.data.since = this.now(); this.save(other);
      this.send(other, { type: "searching", reason: "opponent_left" });
    }
    this.clear(peer, closed ? "closed" : "idle");
  }
  closed(peer) {
    if (peer.data.phase === "closed") return;
    this.release(peer, true); this.pair(); this.broadcastCounts();
  }
  pair() {
    const queued = this.waiting().sort((a,b) => a.data.since - b.data.since || a.data.id.localeCompare(b.data.id));
    while (queued.length > 1) {
      const first = queued.shift();
      const index = queued.findIndex(other => first.data.region === "any" || other.data.region === "any" ||
                                             first.data.region === other.data.region);
      if (index < 0) continue;
      const second = queued.splice(index, 1)[0];
      const match = this.uuid(), deadline = this.now() + 15000;
      for (const [peer, other, role] of [[first,second,"host"],[second,first,"guest"]]) {
        Object.assign(peer.data, { phase: "offered", match, partner: other.data.id, role, accepted: false, deadline });
        this.save(peer);
        this.send(peer, { type: "match_found", match, role, deadline, opponent: other.data.name });
      }
    }
  }
  sweep() {
    let changed = false;
    for (const peer of this.all()) {
      peer.data = peer.socket.deserializeAttachment();
      if (peer.data.phase === "closed") continue;
      if (this.now() - this.fresh(peer) > 75000) {
        this.release(peer, true); try { peer.socket.close(1001, "idle_timeout"); } catch {}
        changed = true;
      }
    }
    const expired = this.all().filter(p => p.data.deadline && p.data.deadline <= this.now());
    for (const peer of expired) {
      peer.data = peer.socket.deserializeAttachment();
      if (!peer.data.deadline || peer.data.deadline > this.now()) continue;
      // The unresponsive participant returns to idle; their responsive partner
      // stays eligible for another opponent without needing a new search click.
      const other = this.partner(peer);
      if (peer.data.phase === "offered" && peer.data.accepted && other && !other.data.accepted) {
        this.release(other, false); this.send(other, { type: "idle", reason: "accept_timeout" });
      } else {
        this.release(peer, false); this.send(peer, { type: "idle", reason: "connection_timeout" });
      }
      changed = true;
    }
    if (changed) { this.pair(); this.broadcastCounts(); }
  }
  needsAlarm() { return this.all().some(p => p.data.phase === "queued" || p.data.deadline > 0); }
}

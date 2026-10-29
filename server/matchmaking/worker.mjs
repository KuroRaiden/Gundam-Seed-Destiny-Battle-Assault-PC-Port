import { DurableObject } from "cloudflare:workers";
import { DiscordQueueAnnouncer } from "./discord.mjs";
import { LobbyCore, PROTOCOL, COMPATIBILITY, SUPPORTED_BUILDS, REGIONS } from "./core.mjs";
function json(value, status = 200) {
  return Response.json(value, { status, headers: { "Cache-Control": "no-store" } });
}
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method !== "GET") return json({ error: "method_not_allowed" }, 405);
    if (url.pathname === "/" || url.pathname === "/health")
      return json({ service: "seedpc-matchmaking", status: "ok", protocol: PROTOCOL,
                    compatibility: COMPATIBILITY, supported_builds: [...SUPPORTED_BUILDS], gameplay_relay: false });
    if (!["/v1/connect", "/v1/status"].includes(url.pathname)) return json({ error: "not_found" }, 404);
    const object = env.MATCHMAKER.get(env.MATCHMAKER.idFromName("public-lobby-v1"));
    return object.fetch(request);
  }
};
export class Matchmaker extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.discord = new DiscordQueueAnnouncer({ storage: ctx.storage,
      queue: () => this.core(false).waiting("any",null).map(peer => peer.data), env });
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
  }
  peers() {
    return this.ctx.getWebSockets().map(socket => ({ socket, data: socket.deserializeAttachment() }));
  }
  core(notify = true) {
    return new LobbyCore(() => this.peers(), { fresh: peer => Math.max(peer.data.lastSeen,
      this.ctx.getWebSocketAutoResponseTimestamp(peer.socket)?.getTime() ?? 0),
      notify: notify ? () => this.countsSoon() : undefined });
  }
  countsSoon() {
    if (this.countPending) return;
    this.countPending = true;
    this.ctx.waitUntil(new Promise(resolve => setTimeout(() => {
      try { this.core(false).broadcastCounts(); }
      finally { this.countPending = false; resolve(); }
    }, 200)));
  }
  async schedule(core) {
    if (core.needsAlarm() || await this.discord.needsAlarm()) {
      const current = await this.ctx.storage.getAlarm();
      if (current === null || current > Date.now() + 15000)
        await this.ctx.storage.setAlarm(Date.now() + 15000);
    } else await this.ctx.storage.deleteAlarm();
  }
  async fetch(request) {
    const url = new URL(request.url);
    const core = this.core(); core.sweep();
    if (url.pathname === "/v1/status") {
      const region = url.searchParams.get("region") ?? "any";
      const build = url.searchParams.get("build") ?? COMPATIBILITY;
      if (!REGIONS.has(region) || !SUPPORTED_BUILDS.has(build))
        return json({ error: "incompatible_query" }, 400);
      await this.schedule(core);
      return json({ protocol: PROTOCOL, compatibility: build, ...core.counts(region, build) });
    }
    if ((request.headers.get("Upgrade") ?? "").toLowerCase() !== "websocket")
      return json({ error: "websocket_required" }, 426);
    const input = new TextEncoder().encode((request.headers.get("CF-Connecting-IP") ?? "local") +
                                           ":" + new Date().toISOString().slice(0,10));
    const hash = await crypto.subtle.digest("SHA-256", input);
    const ip = [...new Uint8Array(hash)].map(x => x.toString(16).padStart(2,"0")).join("");
    const peers = core.all();
    if (peers.length >= 1024 || peers.filter(p => p.data.ip === ip).length >= 8)
      return json({ error: "connection_limit" }, 429);
    const pair = new WebSocketPair();
    this.ctx.acceptWebSocket(pair[1]);
    core.initialize(pair[1], ip);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }
  async webSocketMessage(socket, message) {
    const data = socket.deserializeAttachment();
    if (!data) { socket.close(1008, "invalid_session"); return; }
    const core = this.core();
    core.receive({ socket, data }, message);
    await this.schedule(core);
  }
  async webSocketClose(socket) {
    const data = socket.deserializeAttachment();
    if (data) { const core = this.core(); core.closed({ socket, data }); await this.schedule(core); }
    try { socket.close(1000, "closed"); } catch {}
  }
  async webSocketError(socket) { await this.webSocketClose(socket); }
  async alarm() {
    const core = this.core(); core.sweep(); core.broadcastCounts();
    // Notification delivery must never block queue messages or P2P gameplay.
    try { await this.discord.tick(); } catch { /* Retry on the next scheduled queue alarm. */ }
    await this.schedule(this.core());
  }
}

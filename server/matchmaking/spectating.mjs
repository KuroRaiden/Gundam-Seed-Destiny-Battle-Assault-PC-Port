import { playerName } from "./core.mjs";

const CHUNK = 120, MAX_FRAMES = 1_000_000, PAGE = 12;
const ID = /^[a-f0-9]{32}$/;
function check(value, reason) { if (!value) throw new Error(reason); }
function integer(value, maximum) { return Number.isSafeInteger(value) && value >= 0 && value <= maximum; }
function send(socket, message) {
  try { socket.send(JSON.stringify({ protocol: 1, ...message })); }
  catch { try { socket.close(1013, "viewer_too_slow"); } catch {} }
}
// Runs in a separate durable instance from Quick Match. Only bounded input
// chunks and replay metadata cross this service. Players never wait for viewers.
export class SpectatorService {
  constructor(storage, sockets, now = Date.now) {
    this.storage = storage; this.sockets = sockets; this.now = now;
  }
  async receive(socket, raw) {
    try {
      check(typeof raw === "string" && new TextEncoder().encode(raw).length <= 4096, "message_too_large");
      const event = JSON.parse(raw), peer = socket.deserializeAttachment();
      check(event && event.protocol === 1 && peer, "invalid_session");
      if (this.now() - peer.rateAt >= 10000) { peer.rateAt = this.now(); peer.count = 0; }
      check(++peer.count <= 300, "rate_limited");
      peer.seen = this.now(); socket.serializeAttachment(peer);
      if (event.type === "list") {
        check(integer(event.page, 63), "invalid_page");
        const rows = [...(await this.storage.list({ prefix: "match/" })).values()]
          .filter(match => !match.ended && this.now() - match.updated < 15000)
          .sort((a, b) => b.started - a.started);
        const matches = rows.slice(event.page * PAGE, (event.page + 1) * PAGE).map(match => ({
          id: match.id, p1: match.p1, p2: match.p2, f1: match.f1, f2: match.f2,
          stage: match.stage, frames: match.frames,
          viewers: this.sockets().filter(s => s.deserializeAttachment()?.watch === match.id).length
        }));
        send(socket, { type: "list", matches, more: rows.length > (event.page + 1) * PAGE });
        return;
      }
      check(typeof event.id === "string" && ID.test(event.id), "invalid_match");
      const key = `match/${event.id}`;
      let match = await this.storage.get(key);
      if (event.type === "publish") {
        check(!peer.publish && !match, "already_publishing");
        check((await this.storage.list({ prefix: "match/" })).size < 128, "live_match_limit");
        check(typeof event.header === "string" && event.header.length >= 64 &&
              event.header.length <= 3072 && /^(?:[a-f0-9]{2})+$/.test(event.header) &&
              event.header.startsWith("5345454452504c3101000000"), "invalid_header");
        check(integer(event.f1, 27) && event.f1 > 0 && integer(event.f2, 27) && event.f2 > 0 &&
              integer(event.stage, 24), "invalid_fighter_or_stage");
        match = { id: event.id, header: event.header, p1: playerName(event.p1), p2: playerName(event.p2),
          f1: event.f1, f2: event.f2, stage: event.stage, frames: 0, ended: false,
          complete: false, crc: 0, started: this.now(), updated: this.now() };
        peer.publish = event.id; socket.serializeAttachment(peer);
        await this.storage.put(key, match);
        send(socket, { type: "published", id: event.id });
      } else if (event.type === "append") {
        check(peer.publish === event.id && match && !match.ended, "publisher_required");
        check(event.from === match.frames && match.frames % CHUNK === 0, "input_sequence");
        check(typeof event.inputs === "string" && /^(?:[a-f0-9]{8})+$/.test(event.inputs) &&
              event.inputs.length <= CHUNK * 8 && match.frames + event.inputs.length / 8 <= MAX_FRAMES,
              "invalid_inputs");
        for (let i = 0; i < event.inputs.length; i += 4)
          check(parseInt(event.inputs.slice(i + 2, i + 4) + event.inputs.slice(i, i + 2), 16) <= 1023,
                "invalid_buttons");
        await this.storage.put(`chunk/${event.id}/${event.from / CHUNK}`, event.inputs);
        match.frames += event.inputs.length / 8; match.updated = this.now();
        await this.storage.put(key, match);
        send(socket, { type: "appended", id: event.id, frames: match.frames });
      } else if (event.type === "end") {
        check(peer.publish === event.id && match && event.frames === match.frames &&
              typeof event.complete === "boolean" && integer(event.crc, 0xffffffff) &&
              (event.complete || event.crc === 0), "invalid_end");
        match.ended = true; match.complete = event.complete; match.crc = event.crc;
        match.updated = this.now();
        await this.storage.put(key, match);
        send(socket, { type: "ended", id: event.id });
      } else if (event.type === "watch") {
        check(match && (!match.ended || this.now() - match.updated < 600000), "match_ended");
        peer.watch = event.id; socket.serializeAttachment(peer);
        send(socket, { type: "watch", id: event.id, header: match.header, frames: match.frames });
      } else if (event.type === "pull") {
        check(peer.watch === event.id && match, "watch_required");
        check(integer(event.from, match.frames) &&
              (event.from % CHUNK === 0 || event.from === match.frames), "input_sequence");
        const inputs = event.from < match.frames
          ? await this.storage.get(`chunk/${event.id}/${event.from / CHUNK}`) : "";
        check(typeof inputs === "string", "missing_inputs");
        const finished = match.ended && event.from + inputs.length / 8 === match.frames;
        send(socket, { type: "inputs", id: event.id, from: event.from, inputs,
          ended: finished, complete: finished && match.complete, crc: finished ? match.crc : 0 });
      } else throw new Error("unknown_message");
    } catch (error) { send(socket, { type: "error", reason: error.message }); }
  }
  async closed(socket) {
    const id = socket.deserializeAttachment()?.publish;
    if (!id) return;
    const key = `match/${id}`, match = await this.storage.get(key);
    if (match && !match.ended) {
      match.ended = true; match.updated = this.now();
      await this.storage.put(key, match);
    }
  }
  async sweep() {
    const rows = await this.storage.list({ prefix: "match/" });
    for (const [key, match] of rows) {
      if (!match.ended && this.now() - match.updated > 15000) {
        match.ended = true; match.updated = this.now();
        await this.storage.put(key, match);
      }
      if (this.now() - match.updated > 600000 &&
          !this.sockets().some(socket => socket.deserializeAttachment()?.watch === match.id)) {
        const chunks = await this.storage.list({ prefix: `chunk/${match.id}/`, limit: 128 });
        if (chunks.size) await this.storage.delete([...chunks.keys()]);
        if (chunks.size < 128) await this.storage.delete(key);
      }
    }
    return rows.size > 0;
  }
}

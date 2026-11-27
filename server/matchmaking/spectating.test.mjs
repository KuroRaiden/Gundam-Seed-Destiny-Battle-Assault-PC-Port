import test from "node:test";
import assert from "node:assert/strict";
import { SpectatorService } from "./spectating.mjs";

function fixture() {
  let now = 1_000_000;
  const values = new Map(), sockets = [];
  const storage = {
    async get(key) { return structuredClone(values.get(key)); },
    async put(key, value) { values.set(key, structuredClone(value)); },
    async delete(keys) { for (const key of Array.isArray(keys) ? keys : [keys]) values.delete(key); },
    async list({ prefix, limit = 1000 }) {
      return new Map([...values].filter(([key]) => key.startsWith(prefix)).slice(0, limit)
        .map(([key, value]) => [key, structuredClone(value)]));
    }
  };
  const service = new SpectatorService(storage, () => sockets, () => now);
  const socket = () => {
    const peer = { data: { rateAt: now, count: 0 }, sent: [],
      serializeAttachment(value) { this.data = structuredClone(value); },
      deserializeAttachment() { return structuredClone(this.data); },
      send(text) { this.sent.push(JSON.parse(text)); } };
    sockets.push(peer); return peer;
  };
  const send = async (peer, event) => {
    await service.receive(peer, JSON.stringify({ protocol: 1, ...event }));
    return peer.sent.at(-1);
  };
  const id = "a".repeat(32);
  const publish = { type: "publish", id, p1: "Kira", p2: "Athrun", f1: 1, f2: 8, stage: 24,
    header: "5345454452504c3101000000" + "00".repeat(300) };
  return { service, storage, socket, send, id, publish, advance(ms) { now += ms; }, sockets };
}
test("late viewers receive the full confirmed match without sending player messages", async () => {
  const f = fixture(), host = f.socket();
  assert.equal((await f.send(host, f.publish)).type, "published");
  await f.send(host, { type: "append", id: f.id, from: 0, inputs: "01000000".repeat(120) });
  await f.send(host, { type: "append", id: f.id, from: 120, inputs: "00000200".repeat(120) });
  const hostCount = host.sent.length;
  const viewer = f.socket(), second = f.socket();
  const list = await f.send(viewer, { type: "list", page: 0 });
  assert.equal(list.matches[0].p1, "Kira"); assert.equal(list.matches[0].frames, 240);
  for (const peer of [viewer, second]) {
    assert.equal((await f.send(peer, { type: "watch", id: f.id })).header, f.publish.header);
    const first = await f.send(peer, { type: "pull", id: f.id, from: 0 });
    assert.equal(first.inputs, "01000000".repeat(120)); assert.equal(first.ended, false);
    const later = await f.send(peer, { type: "pull", id: f.id, from: 120 });
    assert.equal(later.inputs, "00000200".repeat(120));
  }
  assert.equal(host.sent.length, hostCount);
  assert.equal((await f.send(viewer, { type: "list", page: 0 })).matches[0].viewers, 2);
  await f.send(host, { type: "append", id: f.id, from: 240, inputs: "00000000".repeat(5) });
  await f.send(host, { type: "end", id: f.id, frames: 245, complete: true, crc: 1234 });
  const final = await f.send(viewer, { type: "pull", id: f.id, from: 240 });
  assert.equal(final.ended, true); assert.equal(final.complete, true); assert.equal(final.crc, 1234);
  assert.equal((await f.send(second, { type: "list", page: 0 })).matches.length, 0);
});
test("publishing is owned by one socket and input order and button ranges are checked", async () => {
  const f = fixture(), host = f.socket(), attacker = f.socket();
  await f.send(host, f.publish);
  assert.equal((await f.send(attacker, { type: "append", id: f.id, from: 0, inputs: "00000000" })).reason,
    "publisher_required");
  assert.equal((await f.send(host, { type: "append", id: f.id, from: 0, inputs: "ffff0000" })).reason,
    "invalid_buttons");
  assert.equal((await f.send(host, { type: "append", id: f.id, from: 120, inputs: "00000000" })).reason,
    "input_sequence");
  assert.equal((await f.send(attacker, { type: "pull", id: f.id, from: 0 })).reason, "watch_required");
  assert.equal((await f.send(host, { type: "end", id: f.id, frames: 1, complete: true, crc: 123 })).reason,
    "invalid_end");
});
test("host disconnect drains the buffered match and cleanup preserves active tournament viewers", async () => {
  const f = fixture(), host = f.socket(), viewer = f.socket();
  await f.send(host, f.publish);
  await f.send(host, { type: "append", id: f.id, from: 0, inputs: "00000000".repeat(120) });
  await f.send(viewer, { type: "watch", id: f.id });
  await f.service.closed(host);
  const result = await f.send(viewer, { type: "pull", id: f.id, from: 0 });
  assert.equal(result.ended, true); assert.equal(result.complete, false);
  f.advance(700000); await f.service.sweep();
  assert(await f.storage.get(`match/${f.id}`));
  f.sockets.splice(f.sockets.indexOf(viewer), 1); await f.service.sweep();
  assert.equal(await f.storage.get(`match/${f.id}`), undefined);
});

import test from "node:test";
import assert from "node:assert/strict";
import { LobbyCore, PROTOCOL, COMPATIBILITY, validCode, playerName } from "./core.mjs";
class Socket {
  constructor() { this.messages = []; this.closed = false; }
  serializeAttachment(data) { this.data = structuredClone(data); }
  deserializeAttachment() { return structuredClone(this.data); }
  send(text) { if (this.closed) throw Error("closed"); this.messages.push(JSON.parse(text)); }
  close() { this.closed = true; }
  events(type) { return this.messages.filter(m => m.type === type); }
}
function fixture() {
  let time = 100000, next = 0;
  const sockets = [];
  const peers = () => sockets.map(socket => ({ socket, data: socket.deserializeAttachment() }));
  const core = () => new LobbyCore(peers, { now: () => time, uuid: () => `id-${++next}` });
  const send = (socket, event) => core().receive({ socket, data: socket.deserializeAttachment() }, JSON.stringify(event));
  const connect = (name, region="any") => {
    const socket = new Socket(); sockets.push(socket); core().initialize(socket, "test");
    send(socket, { type: "hello", protocol: PROTOCOL, build: COMPATIBILITY, name, region });
    return socket;
  };
  return { sockets, core, send, connect,
    advance(ms) { time += ms; },
    close(socket) { core().closed({ socket, data: socket.deserializeAttachment() }); socket.close(); } };
}
function matched(f) {
  const host = f.connect("Kira"), guest = f.connect("Athrun");
  f.send(host,{type:"enqueue"}); f.send(guest,{type:"enqueue"});
  const match = host.data.match;
  return { host,guest,match };
}
test("UTF-8 names and join-code bounds", () => {
  assert.equal(playerName(" Kira\n"), "Kira");
  assert.equal(playerName("あ".repeat(16)).length, 16);
  assert.throws(() => playerName("あ".repeat(17)));
  assert.throws(() => playerName("\n\t"));
  assert.equal(validCode("SEED1-"+"a".repeat(46)), true);
  for (const code of ["", "SEED1-"+"a".repeat(45), "https://evil.example", "SEED1-"+"?".repeat(46)])
    assert.equal(validCode(code), false);
});
test("one queue ticket per connection and accurate compatible waiting count", () => {
  const f=fixture(), a=f.connect("A");
  assert.equal(f.core().counts().waiting, 0);
  f.send(a,{type:"enqueue"}); f.send(a,{type:"enqueue"});
  assert.equal(f.core().counts().waiting,1);
  f.send(a,{type:"cancel"}); assert.equal(f.core().counts().waiting,0);
});
test("pairing, acceptance, private invitation handoff and ready confirmation", () => {
  const f=fixture(), observer=f.connect("Observer"), {host,guest,match}=matched(f);
  assert.notEqual(host.data.id,guest.data.id);
  assert.equal(host.data.role,"host"); assert.equal(guest.data.role,"guest");
  assert.equal(f.core().counts().waiting,0);
  f.send(host,{type:"accept",match});
  assert.equal(host.events("prepare").length,0);
  f.send(guest,{type:"accept",match});
  assert.equal(host.events("prepare").length,1);
  assert.equal(guest.events("prepare").length,0);
  const code="SEED1-"+"b".repeat(46);
  f.send(guest,{type:"publish",match,code}); assert.equal(host.events("connect").length,0);
  f.send(host,{type:"publish",match,code});
  assert.equal(guest.events("connect").at(-1).code,code);
  assert.equal(JSON.stringify(observer.messages).includes(code),false);
  f.send(host,{type:"connected",match}); f.send(guest,{type:"connected",match});
  assert.equal(host.events("handoff").length,1); assert.equal(guest.events("handoff").length,1);
  assert.equal(host.data.phase,"idle"); assert.equal(guest.data.phase,"idle");
});
test("selected regions and Any eligibility", () => {
  const f=fixture(), eu=f.connect("EU","eu"), na=f.connect("NA","na");
  f.send(eu,{type:"enqueue"});f.send(na,{type:"enqueue"});
  assert.equal(f.core().counts().waiting,2); assert.equal(f.core().counts("eu").eligible,1);
  const any=f.connect("Any");f.send(any,{type:"enqueue"});
  assert.equal(eu.data.phase,"offered");assert.equal(any.data.phase,"offered");
  assert.equal(na.data.phase,"queued");assert.equal(f.core().counts().waiting,1);
});
test("disconnect and cancellation release reservations without orphaned counts", () => {
  const f=fixture(),{host,guest}=matched(f);
  f.close(host); assert.equal(guest.data.phase,"queued"); assert.equal(f.core().counts().waiting,1);
  f.send(guest,{type:"cancel"});assert.equal(f.core().counts().waiting,0);
});
test("expired unaccepted offer preserves the responsive participant", () => {
  const f=fixture(),{host,guest,match}=matched(f);
  f.send(host,{type:"accept",match}); f.advance(16000);f.core().sweep();
  assert.equal(host.data.phase,"queued");assert.equal(guest.data.phase,"idle");
  assert.equal(f.core().counts().waiting,1);
  f.send(guest,{type:"accept",match});assert.equal(guest.events("error").at(-1).reason,"stale_match");
  assert.equal(host.data.phase,"queued");
});
test("dead connections expire and no longer inflate the queue", () => {
  const f=fixture(), a=f.connect("A");f.send(a,{type:"enqueue"});
  f.advance(76000);assert.equal(f.core().counts().waiting,0);f.core().sweep();
  assert.equal(a.data.phase,"closed");assert.equal(a.closed,true);
});
test("wrong builds, binary/oversized messages and excessive commands are rejected", () => {
  const f=fixture(),a=f.connect("A");
  f.send(a,{type:"hello",protocol:1,build:"incompatible",name:"A",region:"any"});
  assert.equal(a.events("error").at(-1).reason,"incompatible_build");
  f.core().receive({socket:a,data:a.deserializeAttachment()},"x".repeat(4097));
  assert.equal(a.closed,true);
  const b=f.connect("B");for(let i=0;i<35;i++)f.send(b,{type:"count"});
  assert.equal(b.closed,true);assert.equal(f.core().counts().waiting,0);
});
test("in-memory reconstruction retains live queue and pending handshakes", () => {
  const f=fixture(),{host,guest,match}=matched(f);
  f.send(host,{type:"accept",match});
  const reconstructed=f.core();reconstructed.receive({socket:guest,data:guest.deserializeAttachment()},JSON.stringify({type:"accept",match}));
  assert.equal(host.events("prepare").length,1);assert.equal(f.core().counts().waiting,0);
});
test("a matchmaking disconnect never resets a playing participant", () => {
  const f=fixture(),{host,guest,match}=matched(f);
  f.send(host,{type:"accept",match});f.send(guest,{type:"accept",match});
  f.send(host,{type:"connected",match}); f.close(guest);
  assert.equal(host.data.phase,"playing");assert.equal(f.core().counts().waiting,0);
});
test("500 simultaneous clients form 250 distinct pairs without double allocation", () => {
  const f=fixture();const players=Array.from({length:500},(_,i)=>f.connect(`Player ${i}`));
  for(const p of players)f.send(p,{type:"enqueue"});
  const pairs=new Map();
  for(const p of players) {
    assert.equal(p.data.phase,"offered");
    const members=pairs.get(p.data.match)??[];members.push(p.data.id);pairs.set(p.data.match,members);
  }
  assert.equal(pairs.size,250);assert.equal(f.core().counts().waiting,0);
  for(const members of pairs.values()){assert.equal(members.length,2);assert.notEqual(members[0],members[1]);}
});

import assert from "node:assert/strict";
import { COMPATIBILITY, LEGACY_COMPATIBILITY, PROTOCOL } from "./core.mjs";
const base = (process.argv[2] ?? "http://127.0.0.1:8788").replace(/\/$/, "");
const sockets = [];
const pause = ms => new Promise(resolve => setTimeout(resolve,ms));
class Client {
  constructor(name, build = COMPATIBILITY) {
    this.build=build;this.name=name;this.messages=[];this.raw=[];
    this.ws=new WebSocket(base.replace(/^http/,"ws")+"/v1/connect");sockets.push(this.ws);
    this.ws.addEventListener("message", e => {
      this.raw.push(e.data);
      if (e.data!=="pong") this.messages.push(JSON.parse(e.data));
    });
  }
  send(event) { this.ws.send(JSON.stringify(event)); }
  async take(type, predicate=()=>true) {
    const until=Date.now()+12000;
    while(Date.now()<until) {
      const at=this.messages.findIndex(m=>m.type===type&&predicate(m));
      if(at>=0)return this.messages.splice(at,1)[0];
      if(this.ws.readyState===WebSocket.CLOSED)throw Error(`${this.name} closed waiting for ${type}`);
      await pause(10);
    }
    throw Error(`${this.name} timed out waiting for ${type}: ${JSON.stringify(this.messages)}`);
  }
  async ping() {
    this.ws.send("ping");
    const until=Date.now()+12000;
    while (!this.raw.includes("pong") && Date.now()<until) await pause(10);
    assert(this.raw.includes("pong"),`${this.name} timed out waiting for pong`);
  }
  async ready() {
    await this.take("welcome");
    this.send({type:"hello",protocol:PROTOCOL,build:this.build,name:this.name,region:"any"});
    await this.take("ready");return this;
  }
}
async function status(build = COMPATIBILITY) { const response=await fetch(base+"/v1/status?build="+encodeURIComponent(build));assert.equal(response.status,200);return response.json(); }
try {
  const health=await fetch(base+"/health").then(r=>r.json());
  assert.equal(health.status,"ok");assert.equal(health.protocol,PROTOCOL);
  if (process.argv.includes("--observe-only")) {
    // Production smoke check: never enqueue or reserve a real opponent.
    for (const build of [COMPATIBILITY,LEGACY_COMPATIBILITY]) {
      const client=await new Client("QA compatibility observer",build).ready();
      await client.ping();
      const count=await status(build);
      assert.equal(count.compatibility,build);
      assert.equal(Number.isInteger(count.waiting),true);
      assert(health.supported_builds.includes(build));
    }
    console.log(JSON.stringify({url:base,health,passed:["fixed and legacy WebSocket greetings","auto-pong","compatible status counters"],enqueued:false}));
  } else {
    const observer=await new Client("QA observer").ready();
    await observer.ping();
    const first=await new Client("QA Kira").ready();
    const second=await new Client("QA Athrun").ready();
    first.send({type:"enqueue"});await first.take("searching");
    assert.equal((await status()).waiting,1);
    await observer.take("count",m=>m.waiting===1);
    second.send({type:"enqueue"});
    const a=await first.take("match_found"),b=await second.take("match_found");
    assert.equal(a.match,b.match);assert.equal(a.role,"host");assert.equal(b.role,"guest");
    assert.equal((await status()).waiting,0);
    first.send({type:"accept",match:a.match});second.send({type:"accept",match:b.match});
    await first.take("prepare");await second.take("preparing");
    const code="SEED1-"+"Q".repeat(46);
    first.send({type:"publish",match:a.match,code});
    assert.equal((await second.take("connect")).code,code);await first.take("published");
    assert.equal(JSON.stringify(observer.messages).includes(code),false);
    first.send({type:"connected",match:a.match});second.send({type:"connected",match:b.match});
    await first.take("handoff");await second.take("handoff");
    first.send({type:"enqueue"});await first.take("searching");
    second.send({type:"enqueue"});await first.take("match_found");await second.take("match_found");
    second.ws.close();await first.take("searching",m=>m.reason==="opponent_left");
    assert.equal((await status()).waiting,1);
    first.send({type:"cancel"});await first.take("idle");
    assert.equal((await status()).waiting,0);
    const legacy=await new Client("QA legacy",LEGACY_COMPATIBILITY).ready();
    first.send({type:"enqueue"});await first.take("searching");
    legacy.send({type:"enqueue"});await legacy.take("searching");
    assert.equal((await status()).waiting,1);
    assert.equal((await status(LEGACY_COMPATIBILITY)).waiting,1);
    await pause(300);
    assert.equal(legacy.messages.some(m=>m.type==="match_found"),false);
    const legacyPartner=await new Client("QA legacy partner",LEGACY_COMPATIBILITY).ready();
    legacyPartner.send({type:"enqueue"});
    const oldMatch=await legacy.take("match_found");
    assert.equal((await legacyPartner.take("match_found")).match,oldMatch.match);
    assert.equal((await status()).waiting,1);
    assert.equal((await status(LEGACY_COMPATIBILITY)).waiting,0);
    first.send({type:"cancel"});await first.take("idle");
    legacy.send({type:"cancel"});await legacy.take("idle");
    legacyPartner.send({type:"cancel"});await legacyPartner.take("idle");
    assert.equal((await fetch(base+"/v1/status?build=wrong")).status,400);
    assert.equal((await fetch(base+"/health",{method:"POST"})).status,405);
    console.log(JSON.stringify({url:base,health,passed:["health","hibernation auto-pong","live queue counter", "two distinct match roles", "private invitation handoff", "connected confirmation", "opponent disconnect and requeue", "cancellation cleanup", "legacy and fixed queue isolation", "legacy pairing preserved", "invalid query rejection"]}));
  }
} finally {
  for(const socket of sockets)if(socket.readyState===WebSocket.OPEN)socket.close();
  await pause(300);
}

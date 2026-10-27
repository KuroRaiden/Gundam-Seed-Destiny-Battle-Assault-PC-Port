import assert from "node:assert/strict";
import { COMPATIBILITY, PROTOCOL } from "./core.mjs";
const base = (process.argv[2] ?? "http://127.0.0.1:8788").replace(/\/$/, "");
const sockets = [];
const pause = ms => new Promise(resolve => setTimeout(resolve,ms));
class Client {
  constructor(name) {
    this.name=name;this.messages=[];this.raw=[];
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
  async ready() {
    await this.take("welcome");
    this.send({type:"hello",protocol:PROTOCOL,build:COMPATIBILITY,name:this.name,region:"any"});
    await this.take("ready");return this;
  }
}
async function status() { const response=await fetch(base+"/v1/status");assert.equal(response.status,200);return response.json(); }
try {
  const health=await fetch(base+"/health").then(r=>r.json());
  assert.equal(health.status,"ok");assert.equal(health.protocol,PROTOCOL);
  const observer=await new Client("QA observer").ready();
  observer.ws.send("ping");await pause(200);assert(observer.raw.includes("pong"));
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
  assert.equal((await fetch(base+"/v1/status?build=wrong")).status,400);
  assert.equal((await fetch(base+"/health",{method:"POST"})).status,405);
  console.log(JSON.stringify({url:base,health,passed:["health","hibernation auto-pong","live queue counter", "two distinct match roles", "private invitation handoff", "connected confirmation", "opponent disconnect and requeue", "cancellation cleanup", "invalid query rejection"]}));
} finally {
  for(const socket of sockets)if(socket.readyState===WebSocket.OPEN)socket.close();
  await pause(300);
}

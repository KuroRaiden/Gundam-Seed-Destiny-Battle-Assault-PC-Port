import assert from "node:assert/strict";
import test from "node:test";
import { COMPATIBILITY } from "./core.mjs";
import { DiscordQueueAnnouncer, DISCORD_STATE_KEY, discordConfig } from "./discord.mjs";
const regions = ["na","sa","eu","as","af","oc"];
const roleIds = Object.fromEntries(regions.map((region,index)=>[region,`20000000000000000${index}`]));
const env = { DISCORD_ALERTS_ENABLED:"true", DISCORD_WEBHOOK_URL:"https://discord.com/api/webhooks/300000000000000001/private-test-webhook-token-1234567890", DISCORD_MATCHMAKING_ROLE_ID:"100000000000000001",
  DISCORD_REGION_ROLE_IDS:JSON.stringify(roleIds) };
function fixture(region="any") {
  let time=15001;
  let queued=[{id:"one",since:0,ip:"private-ip-hash",build:COMPATIBILITY,name:"Kira",region}];
  const values=new Map(), calls=[];
  const storage={get:async key=>structuredClone(values.get(key)),
    put:async(key,value)=>{values.set(key,structuredClone(value));}};
  let responder=()=>Response.json({id:"400000000000000001"});
  const request=async(url,options)=>{calls.push({url,options,body:JSON.parse(options.body)});return responder();};
  const create=()=>new DiscordQueueAnnouncer({storage,queue:()=>queued,env,request,
    now:()=>time});
  return {get queued(){return queued;},set queued(value){queued=value;},calls,storage,create,
    clock:value=>{time=value;},response:fn=>{responder=fn;}};
}
for (const region of ["any",...regions]) test(`only Matchmaking and selected ${region} role are mentioned`,async()=>{
  const f=fixture(region);await f.create().tick();assert.equal(f.calls.length,1);
  const payload=f.calls[0].body;
  assert.deepEqual(payload.allowed_mentions.roles,[env.DISCORD_MATCHMAKING_ROLE_ID,...(region==="any"?[]:[roleIds[region]])]);
  assert.deepEqual(payload.allowed_mentions.parse,[]);
  assert.deepEqual(payload.allowed_mentions.users,[]);
  assert(f.calls[0].url.endsWith("?wait=true"));
  assert.equal(f.calls[0].options.headers.Authorization,undefined);
});
test("disabled or invalid configuration never performs Discord requests",async()=>{
  assert.equal(discordConfig({}),null);
  assert.throws(()=>discordConfig({...env,DISCORD_REGION_ROLE_IDS:"{}"}));
  const f=fixture();const bot=new DiscordQueueAnnouncer({storage:f.storage,queue:()=>f.queued,env:{},request:()=>{throw Error("should not send");}});
  await bot.tick();assert.equal(await bot.needsAlarm(),false);
});
test("instant matches, cancelled searches and short waits never ping",async()=>{
  const f=fixture();f.clock(14000);await f.create().tick();assert.equal(f.calls.length,0);
  f.queued=[];f.clock(20000);await f.create().tick();assert.equal(f.calls.length,0);
});
test("display names cannot inject role, user or everyone pings",async()=>{
  const f=fixture();f.queued[0].name="@everyone <@&100000000000000001> <@123456789012345678> **fake**";
  await f.create().tick();assert.equal(f.calls[0].body.content.includes("@everyone"),false);
  assert.equal(f.calls[0].body.content.includes("<@123456789012345678>"),false);
  assert.deepEqual(f.calls[0].body.allowed_mentions.roles,[env.DISCORD_MATCHMAKING_ROLE_ID]);
});
test("an unchanged queue posts once and cooldowns survive object reconstruction",async()=>{
  const f=fixture("na");await f.create().tick();f.clock(50000);await f.create().tick();assert.equal(f.calls.length,1);
  f.queued=[{...f.queued[0],id:"reconnected",since:30000}];await f.create().tick(); // edit the expired alert
  assert.equal(f.calls.filter(c=>c.options.method==="POST").length,1);
  await f.create().tick();assert.equal(f.calls.filter(c=>c.options.method==="POST").length,1);
  f.clock(200001);await f.create().tick();assert.equal(f.calls.filter(c=>c.options.method==="POST").length,2);
  assert.equal(JSON.stringify(await f.storage.get(DISCORD_STATE_KEY)).includes(env.DISCORD_WEBHOOK_URL),false);
});
test("rate limits honor retry_after and retry only a rejected request",async()=>{
  const f=fixture();f.response(()=>Response.json({retry_after:30},{status:429}));await f.create().tick();
  f.clock(30000);await f.create().tick();assert.equal(f.calls.length,1);
  f.clock(46000);f.response(()=>Response.json({id:"400000000000000002"}));await f.create().tick();
  assert.equal(f.calls.length,2);assert.deepEqual(f.calls[1].body.allowed_mentions.roles,[env.DISCORD_MATCHMAKING_ROLE_ID]);
});
test("uncertain webhook delivery never repeats a role ping after restart",async()=>{
  const f=fixture();f.response(()=>{throw Error("network unavailable");});await f.create().tick();
  f.clock(200001);await f.create().tick();assert.equal(f.calls.length,1);
});
test("permanent permission errors back off instead of hammering Discord",async()=>{
  const f=fixture();f.response(()=>Response.json({},{status:403}));await f.create().tick();
  f.clock(80000);await f.create().tick();assert.equal(f.calls.length,1);
});
test("matched and departed players close their alert without re-pinging",async()=>{
  const f=fixture("eu");const bot=f.create();await bot.tick();assert.equal(await bot.needsAlarm(),true);
  f.queued=[];f.clock(16000);await f.create().tick();assert.equal(f.calls.length,2);
  assert.equal(f.calls[1].options.method,"PATCH");assert.deepEqual(f.calls[1].body.allowed_mentions,{parse:[]});
  assert.equal(await bot.needsAlarm(),false);
});
test("a cancellation during durable job creation is rechecked before posting",async()=>{
  const f=fixture();const put=f.storage.put;f.storage.put=async(key,value)=>{
    await put(key,value);if(value.pending)f.queued=[];
  };
  await f.create().tick();assert.equal(f.calls.length,0);
});
test("multiple waiting players in one region share one notification",async()=>{
  const f=fixture("as");f.queued.push({...f.queued[0],id:"two",ip:"second-ip",name:"Athrun"});
  await f.create().tick();assert.equal(f.calls.length,1);
  assert.match(f.calls[0].body.content,/Players waiting: \*\*2\*\*/);
});

test("one active regional announcement updates counts without additional pings",async()=>{
  const f=fixture("na");await f.create().tick();
  f.queued.push({...f.queued[0],id:"two",ip:"second-ip",name:"Athrun"});f.clock(50000);
  await f.create().tick();assert.equal(f.calls.length,2);assert.equal(f.calls[1].options.method,"PATCH");
  assert.match(f.calls[1].body.content,/Players waiting: \*\*2\*\*/);
  assert.deepEqual(f.calls[1].body.allowed_mentions,{parse:[]});
  f.queued=f.queued.slice(1);await f.create().tick();
  assert.match(f.calls[2].body.content,/Players waiting: \*\*1\*\*/);
  assert.equal(f.calls.filter(call=>call.options.method==="POST").length,1);
});
test("webhook credentials cannot redirect requests to other hosts or paths",()=>{
  for(const url of ["https://example.com/api/webhooks/300000000000000001/abcdefghijklmnopqrstuvwxyz1234567890",
    "https://discord.com.evil.example/api/webhooks/300000000000000001/abcdefghijklmnopqrstuvwxyz1234567890",
    "http://discord.com/api/webhooks/300000000000000001/abcdefghijklmnopqrstuvwxyz1234567890",
    env.DISCORD_WEBHOOK_URL+"?other=1"])
    assert.throws(()=>discordConfig({...env,DISCORD_WEBHOOK_URL:url}));
});

import assert from "node:assert/strict";
import test from "node:test";
import {githubReleaseWebhook,ReleaseAnnouncer,releaseAnnouncement} from "./github-releases.mjs";
const env={GITHUB_RELEASE_WEBHOOK_SECRET:"a".repeat(64),
  DISCORD_RELEASE_WEBHOOK_URL:"https://discord.com/api/webhooks/300000000000000001/private-test-webhook-token-1234567890"};
const release={id:123,url:"https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/tag/v0.1.6",
  tag:"v0.1.6",name:"0.1.6",notes:"Palette previews and Discord announcements",prerelease:false};
const payload={action:"published",repository:{full_name:"KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port"},
  release:{id:release.id,html_url:release.url,tag_name:release.tag,name:release.name,body:release.notes,draft:false}};
async function request(body=payload,event="release"){
  const text=JSON.stringify(body),bytes=new TextEncoder().encode(text);
  const key=await crypto.subtle.importKey("raw",new TextEncoder().encode(env.GITHUB_RELEASE_WEBHOOK_SECRET),
    {name:"HMAC",hash:"SHA-256"},false,["sign"]);
  const signed=await crypto.subtle.sign("HMAC",key,bytes);
  const signature=Array.from(new Uint8Array(signed),value=>value.toString(16).padStart(2,"0")).join("");
  return new Request("https://example.com/v1/github/releases",{method:"POST",body:text,
    headers:{"X-GitHub-Event":event,"X-Hub-Signature-256":"sha256="+signature}});
}
function fixture(){
  const values=new Map(),calls=[];
  const storage={get:async key=>structuredClone(values.get(key)),
    put:async(key,value)=>values.set(key,structuredClone(value)),delete:async key=>values.delete(key)};
  let responder=()=>Response.json({id:"400000000000000001"});
  const create=()=>new ReleaseAnnouncer({storage,env,request:async(url,options)=>{
    calls.push({url,options,body:JSON.parse(options.body)});return responder();}});
  return {storage,calls,create,response:fn=>responder=fn};
}
function fixedPayload(version="0.1.6-fixed"){
  const zip=`GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-${version}.zip`;
  return {...structuredClone(payload),action:"edited",release:{...payload.release,
    name:version.replace("-"," "),assets:[zip,zip+".sha256"].map(name=>({name,state:"uploaded",
      size:123,digest:"sha256:"+"b".repeat(64),
      browser_download_url:`https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/download/${release.tag}/${name}`}))}};
}
async function deliver(body,bot){
  return githubReleaseWebhook(await request(body),env,notice=>bot.publish(notice));
}
test("valid published event is authenticated and reduced to release fields",async()=>{
  let notice;const response=await githubReleaseWebhook(await request(),env,async item=>{
    notice=item;return {http_status:200,status:"sent"};});
  assert.equal(response.status,200);assert.deepEqual(notice,release);
});
test("invalid signature cannot post a release",async()=>{
  const signed=await request();signed.headers.set("X-Hub-Signature-256","sha256="+"0".repeat(64));
  assert.equal((await githubReleaseWebhook(signed,env,()=>assert.fail())).status,401);
});
test("signed ping and non-publication events do not post",async()=>{
  for(const [body,event] of [[payload,"ping"],[{...payload,action:"edited"},"release"],
    [{...payload,release:{...payload.release,draft:true}},"release"],[payload,"push"]])
    assert.equal((await githubReleaseWebhook(await request(body,event),env,()=>assert.fail())).status,204);
});
test("other repositories and non-GitHub release links are rejected",async()=>{
  for(const [body,status] of [[{...payload,repository:{full_name:"other/repo"}},403],
    [{...payload,release:{...payload.release,html_url:"https://evil.example/release"}},400]])
    assert.equal((await githubReleaseWebhook(await request(body),env,()=>assert.fail())).status,status);
});
test("missing release configuration cannot accept events",async()=>{
  assert.equal((await githubReleaseWebhook(await request(),{},()=>assert.fail())).status,503);
});
test("ordinary messages retain Lacus identity and disable unrequested mentions",async()=>{
  const f=fixture();assert.equal((await f.create().publish(release)).status,"sent");
  assert.equal(f.calls[0].body.username,undefined);assert.equal(f.calls[0].body.avatar_url,undefined);
  assert.deepEqual(f.calls[0].body.allowed_mentions,{parse:[]});
  assert.equal(f.calls[0].options.redirect,"manual");assert(f.calls[0].url.endsWith("?wait=true"));
});
test("repeated deliveries and concurrent deliveries announce once",async()=>{
  const f=fixture(),bot=f.create();await Promise.all([bot.publish(release),bot.publish(release)]);
  await f.create().publish(release);assert.equal(f.calls.length,1);
});
test("definitely rejected notices can be redelivered",async()=>{
  const f=fixture();f.response(()=>Response.json({},{status:429}));
  assert.equal((await f.create().publish(release)).http_status,502);
  f.response(()=>Response.json({id:"400000000000000001"}));
  assert.equal((await f.create().publish(release)).status,"sent");assert.equal(f.calls.length,2);
});
test("uncertain delivery and interrupted delivery do not duplicate a notice",async()=>{
  const f=fixture();f.response(()=>{throw Error("network timeout");});
  await f.create().publish(release);await f.create().publish(release);assert.equal(f.calls.length,1);
  await f.storage.put("github-release:456",{status:"sending"});
  assert.equal((await f.create().publish({...release,id:456})).http_status,202);
  assert.equal(f.calls.length,1);
});
test("pre-release notices are labelled",()=>{
  assert.match(releaseAnnouncement({...release,prerelease:true}).embeds[0].title,/^Pre-release:/);
});

test("completed edited hotfix is announced after the original release",async()=>{
  const f=fixture(),bot=f.create();await deliver(payload,bot);
  const response=await deliver(fixedPayload(),bot);
  assert.equal((await response.json()).status,"sent");assert.equal(f.calls.length,2);
  assert.equal(f.calls[1].body.embeds[0].title,"Release: 0.1.6 fixed");
});

test("repeated and concurrent hotfix edits survive reconstruction without duplicate posts",async()=>{
  const f=fixture(),bot=f.create(),body=fixedPayload();
  await Promise.all([deliver(body,bot),deliver(body,bot)]);
  body.release.body="Updated instructions";await deliver(body,f.create());
  assert.equal(f.calls.length,1);
  assert.equal((await f.storage.get("github-release:123:0.1.6-fixed")).status,"sent");
});

test("each completed fixed revision is announced once",async()=>{
  const f=fixture();await deliver(fixedPayload(),f.create());
  await deliver(fixedPayload("0.1.6-fixed2"),f.create());
  await deliver(fixedPayload("0.1.6-fixed2"),f.create());
  assert.equal(f.calls.length,2);
});

test("publishing and editing the same completed hotfix share a delivery key",async()=>{
  const f=fixture(),body=fixedPayload();
  await deliver({...body,action:"published"},f.create());await deliver(body,f.create());
  assert.equal(f.calls.length,1);
});

test("incomplete, temporary, mismatched or unverified hotfix files are ignored",async()=>{
  const mutations=[
    body=>body.release.assets.pop(),
    body=>body.release.assets.push({...body.release.assets[0],name:"old.zip"}),
    body=>body.release.assets[0].name+=".replacement-123",
    body=>body.release.assets[1].name="other.zip.sha256",
    body=>body.release.assets[0].state="new",
    body=>body.release.assets[1].size=0,
    body=>delete body.release.assets[0].digest,
    body=>body.release.assets[1].browser_download_url="https://evil.example/checksum",
    body=>body.release.name="0.1.6",
    body=>body.release.tag_name="v0.1.5",
    body=>body.release.draft=true,
  ];
  for(const mutate of mutations){
    const body=fixedPayload();mutate(body);
    assert.equal((await githubReleaseWebhook(await request(body),env,()=>assert.fail())).status,204);
  }
});

test("ordinary release edits and unsupported actions stay silent",async()=>{
  const f=fixture();await deliver(payload,f.create());
  await deliver({...payload,action:"edited",release:{...payload.release,body:"Corrected typo"}},f.create());
  assert.equal((await deliver({...fixedPayload(),action:"released"},f.create())).status,204);
  assert.equal(f.calls.length,1);
});

test("hotfix delivery uncertainty also prevents duplicate announcements",async()=>{
  const f=fixture();f.response(()=>{throw Error("timeout after sending");});
  const response=await deliver(fixedPayload(),f.create());
  assert.equal(response.status,202);
  await deliver(fixedPayload(),f.create());assert.equal(f.calls.length,1);
});

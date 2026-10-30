// Exercise the real native-fetch path in the installed Workers runtime.
// Usage: node tools/verify_discord_worker.mjs <directory containing Wrangler's node_modules>
// Outbound requests are intercepted locally; no Discord credentials are used.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const tooling=process.argv[2];
if(!tooling)throw Error("Pass the directory containing the installed Wrangler package.json.");
const require=createRequire(pathToFileURL(resolve(tooling,"package.json")));
const {Miniflare,convertV4MiniflareOptions}=require("miniflare");
const {buildSync}=require("esbuild");
const root=fileURLToPath(new URL("../",import.meta.url));
const script=buildSync({stdin:{resolveDir:root,contents:`
import {DiscordQueueAnnouncer} from './server/matchmaking/discord.mjs';
import {COMPATIBILITY} from './server/matchmaking/core.mjs';
import {ReleaseAnnouncer,githubReleaseWebhook} from './server/matchmaking/github-releases.mjs';
export default {async fetch(){
  const values=new Map();
  const storage={get:async key=>structuredClone(values.get(key)),
    put:async(key,value)=>values.set(key,structuredClone(value))};
  let queued=[{id:'test-player',since:0,ip:'test-ip-hash',build:COMPATIBILITY,region:'na'}];
  const env={DISCORD_ALERTS_ENABLED:'true',
    DISCORD_WEBHOOK_URL:'https://discord.com/api/webhooks/300000000000000001/private-test-webhook-token-1234567890',
    DISCORD_MATCHMAKING_ROLE_ID:'100000000000000001',
    DISCORD_REGION_ROLE_IDS:JSON.stringify(Object.fromEntries(
      ['na','sa','eu','as','af','oc'].map((region,index)=>[region,'20000000000000000'+index])))};
  // Intentionally omit request: this must use the Worker's native global fetch.
  const bot=new DiscordQueueAnnouncer({storage,queue:()=>queued,env,now:()=>15001});
  const probe=await bot.diagnostics();
  await bot.tick();
  queued=[];await bot.tick();
  const releaseEnv={DISCORD_RELEASE_WEBHOOK_URL:env.DISCORD_WEBHOOK_URL,
    GITHUB_RELEASE_WEBHOOK_SECRET:'a'.repeat(64)};
  const releasePayload=JSON.stringify({action:'published',
    repository:{full_name:'KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port'},
    release:{id:123,tag_name:'v0.1.6',name:'0.1.6',body:'Release test',
      html_url:'https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port/releases/tag/v0.1.6'}});
  const signingKey=await crypto.subtle.importKey('raw',new TextEncoder().encode(releaseEnv.GITHUB_RELEASE_WEBHOOK_SECRET),
    {name:'HMAC',hash:'SHA-256'},false,['sign']);
  const signature=Array.from(new Uint8Array(await crypto.subtle.sign('HMAC',signingKey,new TextEncoder().encode(releasePayload))),
    value=>value.toString(16).padStart(2,'0')).join('');
  const poster=new ReleaseAnnouncer({storage,env:releaseEnv});
  const releaseResult=await githubReleaseWebhook(new Request('https://example.com/v1/github/releases',
    {method:'POST',body:releasePayload,headers:{'X-GitHub-Event':'release','X-Hub-Signature-256':'sha256='+signature}}),
    releaseEnv,notice=>poster.publish(notice));
  return Response.json({probe,final:await bot.diagnostics(),release:await releaseResult.json()});
}};`},bundle:true,format:"esm",platform:"browser",write:false}).outputFiles[0].text;
const requests=[];
const options={modules:true,script,compatibilityDate:"2026-10-27",rootPath:process.cwd(),
  outboundService:async request=>{
    const body=request.method==="GET"?null:await request.json();
    requests.push({method:request.method,userAgent:request.headers.get("User-Agent"),body});
    return Response.json({id:"400000000000000001"});
  }};
// Miniflare 5 provides a converter for the established V4 options format.
const mf=new Miniflare(convertV4MiniflareOptions?convertV4MiniflareOptions(options):options);
try{
  const response=await mf.dispatchFetch("http://localhost/");
  assert.equal(response.status,200);
  const result=await response.json();
  if(result.probe.webhook.status!=="reachable")console.log(JSON.stringify({runtimeProbe:result,interceptedMethods:requests.map(request=>request.method)}));
  assert.equal(result.probe.webhook.status,"reachable");
  assert.equal(result.final.delivery.status,"delivered");
  assert.equal(result.release.status,"sent");
  assert.deepEqual(requests.map(request=>request.method),["GET","POST","PATCH","POST"]);
  for(const request of requests)assert.match(request.userAgent,/^DiscordBot /);
  assert.equal(requests[1].body.allowed_mentions.roles.length,2);
  assert.deepEqual(requests[2].body.allowed_mentions,{parse:[]});
  assert.equal(requests[3].body.username,undefined);
  assert.equal(requests[3].body.avatar_url,undefined);
  console.log(JSON.stringify({passed:["Workers native fetch receiver","readiness probe",
    "NA queue announcement","cancelled queue closes message","no repeated role mention",
    "GitHub HMAC verification","release post preserves selected Lacus identity"],
    realDiscordRequests:0}));
}finally{await mf.dispose();}

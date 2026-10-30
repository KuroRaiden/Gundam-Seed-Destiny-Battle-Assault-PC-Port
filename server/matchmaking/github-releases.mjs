// Authenticated GitHub release events -> ordinary Discord webhook messages.
// The ordinary endpoint preserves the webhook owner's selected name/avatar.
const REPOSITORY="KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port";
const RELEASE_URL=`https://github.com/${REPOSITORY}/releases/tag/`;
const encoder=new TextEncoder();
const USER_AGENT=`DiscordBot (https://github.com/${REPOSITORY}, 0.1.6)`;

function webhookUrl(value){
  let url;try{url=new URL(value);}catch{return null;}
  return url.protocol==="https:" && url.hostname==="discord.com" && !url.port &&
    !url.username && !url.password && !url.search && !url.hash &&
    /^\/api\/(?:v10\/)?webhooks\/[0-9]{17,20}\/[A-Za-z0-9._-]{32,256}$/.test(url.pathname)?url.href:null;
}
export function releaseBridgeReady(env){
  return !!webhookUrl(env.DISCORD_RELEASE_WEBHOOK_URL) &&
    /^[a-f0-9]{64}$/.test(env.GITHUB_RELEASE_WEBHOOK_SECRET??"");
}
async function bodyBytes(request){
  if(!request.body)return new Uint8Array();
  const reader=request.body.getReader(),chunks=[];let size=0;
  for(;;){const {done,value}=await reader.read();if(done)break;
    size+=value.length;if(size>262144){await reader.cancel();return null;}chunks.push(value);}
  const bytes=new Uint8Array(size);let at=0;
  for(const chunk of chunks){bytes.set(chunk,at);at+=chunk.length;}return bytes;
}
export async function githubReleaseWebhook(request,env,publish){
  if(request.method!=="POST")return new Response(null,{status:405});
  if(!releaseBridgeReady(env))return new Response("Release notifications unavailable",{status:503});
  const signature=request.headers.get("X-Hub-Signature-256")??"";
  if(!/^sha256=[a-f0-9]{64}$/i.test(signature))return new Response(null,{status:401});
  const bytes=await bodyBytes(request);if(bytes===null)return new Response(null,{status:413});
  const key=await crypto.subtle.importKey("raw",encoder.encode(env.GITHUB_RELEASE_WEBHOOK_SECRET),
    {name:"HMAC",hash:"SHA-256"},false,["verify"]);
  const signed=Uint8Array.from(signature.slice(7).match(/../g),value=>parseInt(value,16));
  if(!await crypto.subtle.verify("HMAC",key,signed,bytes))return new Response(null,{status:401});
  const event=request.headers.get("X-GitHub-Event");
  if(event==="ping")return new Response(null,{status:204});
  if(event!=="release")return new Response(null,{status:204});
  let payload;try{payload=JSON.parse(new TextDecoder("utf-8",{fatal:true}).decode(bytes));}
  catch{return new Response(null,{status:400});}
  if(payload.repository?.full_name?.toLowerCase()!==REPOSITORY.toLowerCase())
    return new Response(null,{status:403});
  if(payload.action!=="published" || payload.release?.draft)return new Response(null,{status:204});
  const release=payload.release;
  if(!Number.isSafeInteger(release?.id) || release.id<=0 ||
      typeof release.html_url!=="string" || !release.html_url.startsWith(RELEASE_URL) ||
      typeof release.tag_name!=="string" || !release.tag_name || release.tag_name.length>100)
    return new Response(null,{status:400});
  const notice={id:release.id,url:release.html_url,tag:release.tag_name,
    name:String(release.name||release.tag_name).slice(0,200),
    notes:String(release.body??"").slice(0,1800),prerelease:release.prerelease===true};
  const result=await publish(notice);
  return Response.json(result,{status:result.http_status,headers:{"Cache-Control":"no-store"}});
}
export function releaseAnnouncement(release){
  return {content:"A new Gundam SEED PC update is available!",allowed_mentions:{parse:[]},
    embeds:[{title:`${release.prerelease?"Pre-release":"Release"}: ${release.name}`,
      url:release.url,description:release.notes||"Open the release page for the download and release notes.",
      color:0xf79cc9,footer:{text:"Gundam SEED Destiny / Battle Assault PC"}}]};
}
export class ReleaseAnnouncer{
  constructor({storage,env,request=(...args)=>fetch(...args),now=Date.now}){
    this.storage=storage;this.env=env;this.request=request;this.now=now;
    this.inFlight=new Map();
  }
  publish(release){
    if(this.inFlight.has(release.id))return this.inFlight.get(release.id);
    const job=this.publishOnce(release).finally(()=>this.inFlight.delete(release.id));
    this.inFlight.set(release.id,job);return job;
  }
  async publishOnce(release){
    const url=webhookUrl(this.env.DISCORD_RELEASE_WEBHOOK_URL);
    if(!url)return {status:"configuration_error",http_status:503};
    const key=`github-release:${release.id}`,previous=await this.storage.get(key);
    if(previous)return {status:previous.status,message_id:previous.message_id,
      http_status:previous.status==="sent"?200:202};
    // Persist before HTTP. A restart or ambiguous POST must not repeat a notice.
    await this.storage.put(key,{status:"sending",at:this.now()});
    try{
      const response=await this.request(url+"?wait=true",{method:"POST",redirect:"manual",
        signal:AbortSignal.timeout(5000),headers:{"Content-Type":"application/json","User-Agent":USER_AGENT},
        body:JSON.stringify(releaseAnnouncement(release))});
      let result={};try{result=await response.json();}catch{}
      if(response.ok && /^[0-9]{17,20}$/.test(result.id??"")){
        await this.storage.put(key,{status:"sent",message_id:result.id,at:this.now()});
        return {status:"sent",message_id:result.id,http_status:200};
      }
      if(response.status>=400 && response.status<500){
        await this.storage.delete(key); // Rejected requests are safe to redeliver.
        return {status:"discord_rejected",discord_status:response.status,http_status:502};
      }
    }catch{ /* No raw error or URL is exposed to callers or logs. */ }
    await this.storage.put(key,{status:"delivery_uncertain",at:this.now()});
    return {status:"delivery_uncertain",http_status:202};
  }
}

// Automatic queue announcements through a channel-scoped Discord webhook.
// No bot token, Discord account, AI model or client-side credentials are needed.
import { LEGACY_COMPATIBILITY, REGIONS } from "./core.mjs";
export const DISCORD_STATE_KEY = "discord-webhook-queue-v1";
const PROBE_KEY = "discord-webhook-probe-v1", DELIVERY_KEY = "discord-webhook-delivery-v1";
const USER_AGENT = "DiscordBot (https://github.com/KuroRaiden/Gundam-Seed-Destiny-Battle-Assault-PC-Port, 0.1.6)";
export const QUEUE_NOTICE_DELAY = 15000;
const GLOBAL_COOLDOWN = 30000, REGION_COOLDOWN = 180000, PLAYER_COOLDOWN = 180000;
const snowflake = value => typeof value === "string" && /^[0-9]{17,20}$/.test(value) &&
  BigInt(value) <= 0xffffffffffffffffn;
const ticket = peer => `${peer.id}:${peer.since}`;
const keyOf = peer => `${peer.build}:${peer.region}`;
const regionNames = {any:"Any region",na:"North America",sa:"South America",eu:"Europe",
  as:"Asia",af:"Africa",oc:"Oceania"};
const signature = peers => peers.map(ticket).sort().join(",");
export function discordConfig(env) {
  if (env.DISCORD_ALERTS_ENABLED !== "true") return null;
  let url, roles;
  try { url=new URL(env.DISCORD_WEBHOOK_URL); }
  catch { throw Error("discord_webhook_invalid"); }
  if (url.protocol !== "https:" || url.hostname !== "discord.com" || url.port || url.username ||
      url.password || url.search || url.hash ||
      !/^\/api\/(?:v10\/)?webhooks\/[0-9]{17,20}\/[A-Za-z0-9._-]{32,256}$/.test(url.pathname))
    throw Error("discord_webhook_invalid");
  if (!snowflake(env.DISCORD_MATCHMAKING_ROLE_ID))
    throw Error("discord_matchmaking_role_invalid");
  try { roles=JSON.parse(env.DISCORD_REGION_ROLE_IDS); }
  catch { throw Error("discord_region_roles_invalid"); }
  if (!roles || Array.isArray(roles) ||
      typeof roles !== "object" || Object.keys(roles).length !== 6 ||
      [...REGIONS].filter(r=>r!=="any").some(r=>!snowflake(roles[r])) ||
      new Set([env.DISCORD_MATCHMAKING_ROLE_ID,...Object.values(roles)]).size !== 7)
    throw Error("discord_region_roles_invalid");
  return {webhook:url.href,matchmakingRole:env.DISCORD_MATCHMAKING_ROLE_ID,roles};
}
// Configuration diagnostics contain fixed codes only, never secret values.
export function discordHealth(env) {
  try { return {status:discordConfig(env)?"ready":"disabled"}; }
  catch (error) { return {status:"configuration_error",reason:error.message}; }
}
export function announcement(config, peers, ping = true) {
  const region=peers[0].region;
  const roles=[config.matchmakingRole,...(region==="any"?[]:[config.roles[region]])];
  const build=peers[0].build===LEGACY_COMPATIBILITY?"0.1.4":"0.1.5 or a compatible update";
  const mentions=ping?roles.map(id=>`<@&${id}>`).join(" ")+"\n":"";
  return {content:`${mentions}**QUICK MATCH — OPPONENT WANTED**\nPlayers waiting: **${peers.length}**\nRegion: **${regionNames[region]}**\nQueue status: **Searching**\nGame build: ${build}.\nLaunch the game → **VS Mode → Quick Match → Find Match**.`,
    allowed_mentions:ping?{parse:[],roles,users:[],replied_user:false}:{parse:[]}};
}
const initialState=()=>({announced:{},ipAfter:{},regionAfter:{},active:{},globalAfter:0,retryAfter:0,pending:null});
export class DiscordQueueAnnouncer {
  // Workers' native fetch requires its global receiver, unlike Node's fetch.
  constructor({storage,queue,env,request=(...args)=>fetch(...args),now=Date.now}) {
    this.storage=storage;this.queue=queue;this.request=request;this.now=now;
    this.health=discordHealth(env);
    try {this.config=discordConfig(env);} catch {this.config=null;}
  }
  async diagnostics() {
    if(!this.config)return this.health;
    const now=this.now();
    let probe=await this.storage.get(PROBE_KEY);
    if(!probe || now-probe.checked_at>=60000){
      probe={status:"checking",checked_at:now};
      await this.storage.put(PROBE_KEY,probe);
      try{
        const response=await this.request(this.config.webhook,{method:"GET",redirect:"manual",
          signal:AbortSignal.timeout(5000),headers:{"User-Agent":USER_AGENT}});
        probe={status:response.ok?"reachable":"http_error",http_status:response.status,checked_at:now};
        // Only fixed status codes leave this method. Discord metadata stays private.
        await response.body?.cancel();
      }catch{probe={status:"network_error",checked_at:now};}
      await this.storage.put(PROBE_KEY,probe);
    }
    const delivery=await this.storage.get(DELIVERY_KEY)??{status:"not_attempted"};
    return {...this.health,webhook:probe,delivery};
  }
  async needsAlarm() {
    if(!this.config)return false;
    const state=await this.storage.get(DISCORD_STATE_KEY);
    return !!state && (!!state.pending || Object.keys(state.active).length>0);
  }
  async save(state){await this.storage.put(DISCORD_STATE_KEY,state);}
  async send(method,id,body){
    const url=id?`${this.config.webhook}/messages/${id}`:`${this.config.webhook}?wait=true`;
    let response;
    // Workers supports manual/follow redirects. Manual keeps the token on Discord.
    try {response=await this.request(url,{method,redirect:"manual",signal:AbortSignal.timeout(5000),
      headers:{"Content-Type":"application/json","User-Agent":USER_AGENT},body:JSON.stringify(body)});}
    catch(error){
      await this.storage.put(DELIVERY_KEY,{status:"network_error",method,at:this.now()});
      throw error;
    }
    let result={};try{result=await response.json();}catch{}
    const diagnostic={status:response.ok?"delivered":"http_error",method,http_status:response.status,at:this.now()};
    if(Number.isSafeInteger(result.code))diagnostic.discord_code=result.code;
    await this.storage.put(DELIVERY_KEY,diagnostic);
    return {response,result};
  }
  consume(state,job,now){
    for(const item of job.peers){state.announced[ticket(item)]=now;state.ipAfter[item.ip]=now+PLAYER_COOLDOWN;}
    state.regionAfter[job.key]=now+REGION_COOLDOWN;state.globalAfter=now+GLOBAL_COOLDOWN;
    state.pending=null;
  }
  async tick(){
    if(!this.config)return;
    const now=this.now(),state=await this.storage.get(DISCORD_STATE_KEY)??initialState();
    let queued=this.queue();
    // Webhooks have no documented POST idempotency key. A saved pending send
    // after a restart is ambiguous: suppress another ping for those tickets.
    // PATCH retries are safe because the existing message ID is known.
    if(state.pending)this.consume(state,state.pending,now);
    const current=new Set(queued.map(ticket));
    for(const item of Object.keys(state.announced))if(!current.has(item))delete state.announced[item];
    for(const map of [state.ipAfter,state.regionAfter])
      for(const item of Object.keys(map))if(map[item]<=now)delete map[item];
    if(state.retryAfter>now){await this.save(state);return;}
    for(const [key,notice] of Object.entries(state.active)){
      const peers=queued.filter(peer=>keyOf(peer)===key);
      const continuing=peers.some(peer=>notice.tickets.includes(ticket(peer)));
      if(continuing && notice.signature===signature(peers))continue;
      const body=continuing?announcement(this.config,peers,false):{
        content:"**Quick Match alert closed**\nPlayers waiting in this search: **0**\nThese players found a match or left the queue.",
        allowed_mentions:{parse:[]}};
      try{
        const {response,result}=await this.send("PATCH",notice.id,body);
        if(response.ok){
          if(continuing){notice.signature=signature(peers);notice.tickets=peers.map(ticket);}
          else delete state.active[key];
        }else if(response.status===404)delete state.active[key];
        else state.retryAfter=now+this.retryDelay(response,result);
      }catch{state.retryAfter=now+15000;}
      await this.save(state);return; // Bound work and Discord requests per alarm.
    }
    if(state.globalAfter>now){await this.save(state);return;}
    const eligible=queued.filter(peer=>now-peer.since>=QUEUE_NOTICE_DELAY &&
      !state.active[keyOf(peer)] && !state.announced[ticket(peer)] &&
      !(state.ipAfter[peer.ip]>now) && !(state.regionAfter[keyOf(peer)]>now))
      .sort((a,b)=>a.since-b.since || a.id.localeCompare(b.id));
    if(!eligible.length){await this.save(state);return;}
    const key=keyOf(eligible[0]);
    // Include the real full waiting count, even if some tickets joined recently.
    let peers=queued.filter(peer=>keyOf(peer)===key);
    const job={key,peers:peers.map(peer=>({id:peer.id,since:peer.since,ip:peer.ip}))};
    state.pending=job;await this.save(state);
    queued=this.queue();peers=queued.filter(peer=>job.peers.some(item=>ticket(item)===ticket(peer)));
    if(!peers.length){state.pending=null;await this.save(state);return;}
    try{
      const {response,result}=await this.send("POST",null,announcement(this.config,peers));
      if(response.ok && snowflake(result.id)){
        state.active[key]={id:result.id,tickets:peers.map(ticket),signature:signature(peers)};
        this.consume(state,job,now);state.retryAfter=0;
      }else if(response.status===429 || (response.status>=400 && response.status<500)){
        state.pending=null;state.retryAfter=now+this.retryDelay(response,result);
      }else{
        // Do not duplicate a possibly delivered role ping after an uncertain POST.
        this.consume(state,job,now);state.retryAfter=now+15000;
      }
    }catch{this.consume(state,job,now);state.retryAfter=now+15000;}
    await this.save(state);
  }
  retryDelay(response,result){
    if(response.status===429){
      const seconds=Number(result.retry_after??response.headers.get("retry-after"));
      return Number.isFinite(seconds)&&seconds>0?Math.max(1000,seconds*1000):60000;
    }
    return response.status>=400&&response.status<500?900000:15000;
  }
}

// Runs against a local Wrangler instance. Uses original ROM/BIOS files in place.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { createWriteStream } from "node:fs";
import path from "node:path";

const [build, rom, bios, output, base = "http://127.0.0.1:8794"] = process.argv.slice(2);
assert(build && rom && bios && output, "Pass build, JP ROM, BIOS and E: output directory");
await mkdir(output, { recursive: true });
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const children = [], sockets = [];
const env = { ...process.env, SDL_VIDEO_DRIVER: "dummy", SDL_RENDER_DRIVER: "software",
  SDL_AUDIO_DRIVER: "dummy", SEEDPC_SPECTATOR_FIXTURE: base + "/v1/spectate" };
function run(name, executable, args) {
  const log = createWriteStream(path.join(output, name + ".log"));
  const child = spawn(path.join(build, executable), args, { cwd: build, env, windowsHide: true,
    stdio: ["ignore", "pipe", "pipe"] });
  child.stdout.pipe(log); child.stderr.pipe(log);
  children.push(child);
  child.done = new Promise((resolve, reject) => {
    child.on("error", reject);
    child.on("exit", code => { log.end(); resolve(code); });
  });
  return child;
}
async function connect() {
  const socket = new WebSocket(base.replace(/^http/, "ws") + "/v1/spectate");
  sockets.push(socket);
  socket.messages = [];
  socket.addEventListener("message", event => {
    if (event.data !== "pong") socket.messages.push(JSON.parse(event.data));
  });
  await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
  });
  return socket;
}
async function request(socket, event, type) {
  socket.send(JSON.stringify({ protocol: 1, ...event }));
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const index = socket.messages.findIndex(message => message.type === type || message.type === "error");
    if (index >= 0) {
      const response = socket.messages.splice(index, 1)[0];
      assert.notEqual(response.type, "error", JSON.stringify(response));
      return response;
    }
    await delay(10);
  }
  throw Error("Timed out waiting for " + type);
}
try {
  const host = run("host", "seed_jp_online_module_peer.exe", ["--rom", rom, "--bios", bios,
    "--save", path.join(output, "host.sav"), "--seat", "0", "--net-bind", "46641",
    "--net-peer", "46642", "--check-frames", "1200", "--report", path.join(output, "host.json")]);
  const guest = run("guest", "seed_jp_online_module_peer.exe", ["--rom", rom, "--bios", bios,
    "--save", path.join(output, "guest.sav"), "--seat", "1", "--net-bind", "46642",
    "--net-peer", "46641", "--check-frames", "1200", "--report", path.join(output, "guest.json")]);
  const browser = await connect();
  let match;
  const deadline = Date.now() + 25000;
  while (Date.now() < deadline) {
    const list = await request(browser, { type: "list", page: 0 }, "list");
    match = list.matches.find(row => row.frames >= 360);
    if (match) break;
    await delay(1000);
  }
  assert(match, "Online host never published a confirmed match");
  await writeFile(path.join(output, "live-match.json"), JSON.stringify(match, null, 2));
  // A viewer joins, pulls history, then leaves while both players are still fighting.
  const departing = await connect();
  await request(departing, { type: "watch", id: match.id }, "watch");
  const initial = await request(departing, { type: "pull", id: match.id, from: 0 }, "inputs");
  assert.equal(initial.inputs.length, 960);
  departing.close();
  assert.equal(host.exitCode, null); assert.equal(guest.exitCode, null);
  const common = ["--spectate-match", match.id, "--jp-rom", rom, "--bios", bios, "--scale", "1"];
  const mid = run("mid-fight", "GundamSeedRecomp.exe", common);
  const start = run("from-start", "GundamSeedRecomp.exe", [...common, "--spectate-from-start", "--replay-boxes"]);
  const result = await Promise.race([
    Promise.all([host.done, guest.done, mid.done, start.done]),
    new Promise((_, reject) => setTimeout(() => reject(Error("Spectator integration timed out")), 60000).unref())
  ]);
  assert.deepEqual(result, [0, 0, 0, 0], "A player or spectator failed");
  const peer1 = JSON.parse(await readFile(path.join(output, "host.json"), "utf8"));
  const peer2 = JSON.parse(await readFile(path.join(output, "guest.json"), "utf8"));
  assert.equal(peer1.final_crc, peer2.final_crc, "Spectating changed the players' result");
  for (const name of ["mid-fight", "from-start"]) {
    const log = await readFile(path.join(output, name + ".log"), "utf8");
    assert.match(log, /replay_playback: frames=1200 verified=1/, name + " did not verify all match inputs");
    if (name === "mid-fight") assert.match(log, /spectator_catchup_complete: frame=[1-9]/);
    else assert.doesNotMatch(log, /spectator_catchup_complete/);
  }
  console.log("PASS two native players, live list, late joining in both modes, viewer leaving, hitbox overlay and verified final checksums");
} finally {
  for (const socket of sockets) socket.close();
  for (const child of children) if (child.exitCode === null) child.kill();
}

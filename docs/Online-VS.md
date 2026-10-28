# Online VS status

The US main menu's top-middle **VS Mode** now opens a Dear ImGui connection
window in the existing SDL3 game window. Its underlying selection is the same
24-choice JP Destiny grid used for Local VS and Training. The qualified menu
transition redirects state 12 to the JP-backed selector before the old US
cable connection screen can advance. The only combat module used by this
selector is `SeedJpCombat.dll`.

Open **VS Mode** and choose **Quick Match**. Enter your player name, choose a
region (or **Any region**), then **Find match**. Offers are accepted automatically;
when the peer connection is ready, both players enter JP character selection.
**Cancel search** stops pairing while keeping the Online VS window open.

**Players waiting** counts compatible players still in the queue, excluding
players already paired or playing. A selected region also shows its eligible
count. Counts load while the Quick Match tab is open; `--` means they are not
available yet. Observing the count does not enter the queue. Player names are
saved locally; the region selection lasts for the current session.

The native client uses the deployed Cloudflare service over HTTPS/WebSocket
for counts and pairing. It creates the usual AutoPunch room and forwards its
private invitation only to the assigned opponent. Combat inputs, snapshots and
rematches continue directly between peers. After the native room connects,
service failures cannot interrupt selection, combat or rematches. Quick Match
uses the same direct-connection requirements as Host/Join; it adds no game relay.
No Cloudflare account is required to play. Public lobby browsing and delayed
spectating are still separate work.

For a private match, choose **Host**, then **Create code**. The game selects a UDP
port and discovers its public endpoint with STUN on the same socket used for
play. Share **Copy code** or **Copy link** with your opponent. In **Join**, paste
the code or link and choose **Join match**. Players do not enter IP addresses or
ports. A code contains the endpoint and a random room key; treat it as a private
invitation. A fresh room gets a fresh key.

The compact format is `SEED1-<46 URL-safe characters>` and the invitation link is
`seedpc://join/<code>`. Corrupt, truncated, oversized and unsupported codes are
rejected before networking starts. The native AutoPunch-compatible client uses
the pinned upstream rendezvous protocol at `delthas.fr:14763`. STUN uses
`stun.l.google.com:19302`. These services exchange endpoint information;
match traffic flows directly between players. No fallback relay is configured.
If discovery fails, a LAN-only code is offered. Some NATs and firewalls block
direct connections even with hole punching; those connections report a timeout.

**Enable invitation links on this PC** optionally registers this portable
launcher as the current user's `seedpc` handler. Windows can then open a shared
link, start this launcher and join automatically. An existing game receives the
invitation without launching a duplicate. During a match the invitation waits
until the player leaves combat. Moving the portable folder requires enabling
links again from its new location; pasting codes needs no registration.

Rooms use OS-generated random keys, versioned packet envelopes, endpoint pinning,
bounded packet queues and packet CRCs. The listening host sends only a one-byte
NAT probe to unauthenticated rendezvous mappings; it does not disclose the room
key to them. Each battle has a separate input epoch to reject stale game data.

Once connected, the lobby closes and each machine controls its own cursor on
the JP grid using **Player 1 bindings**. The joining machine owns Player 2.
Press **Start** to cycle your fighter's colors/hidden variants before confirming
with A. Selection and match startup verify the same choices on both peers.
Confirm both fighters, then the host chooses and confirms a stage to start JP combat.
Both players see the same stage preview; Random resolves once before startup. The US menu pauses while its JP DLL
borrows the same SDL3 window, renderer, controls and UDP socket. There is one
active simulation per player, with both fighters in it; no GBA cable or second
local simulation is involved. After a completed match, both players return to
JP character selection in the same room. Choose fighters/colors and press A
again for another match. There is no rematch limit and no new code to enter.
Names, palette profiles, the invitation and the existing connection remain
active until a player leaves. Ready flags reset, and held buttons must be
released before confirming the next fighter. B or Esc leaves the room.

Room protocol 9 requires updated builds on both machines. Every match advances
a shared nonzero input epoch; the stage is included in the acknowledged start
contract and combat configuration. Returning from stage select also changes a
host-owned selection revision, rejecting earlier ready packets. In addition, old selection, startup and game packets cannot
ready or start the next match. The selector can acknowledge the previous match's
final checksum while the other peer finishes, avoiding a reconnect when its
last acknowledgement was lost. Failed or unconverged matches still close the
room and show the connection error.

F4 opens remapping during online combat while the simulation continues and
the local input stream is neutral. Native Start pause is suppressed online.
Esc leaves a match or closes the current overlay. Disconnections and checksum
errors end the session with a message. A result must converge at an agreed
future frame before returning from combat. Back, Esc, gamepad B and the lobby
close button return to the US main menu. Lobby text entry captures gameplay
keys, including F4. Closing it restores the input release guard.

## Rollback state and validation

GekkoNet uses two input streams, two frames of local delay, and up to eight
frames of prediction. Native stepping runs at a fully unwound dispatch
boundary. Snapshots contain CPU, timing, call/SWI stacks, memory, devices,
framebuffers and the JP match hook state. The canonical device codec includes
all PSG channels and wave RAM. The older user-save codec omitted audio state
and failed replay; it is not used for rollback. Host audio queues are excluded,
and replayed audio is discarded. Each peer starts with a fixed emulated clock
and an isolated temporary cartridge save, then verifies the initial checksum.

Completed local-machine checks are recorded in `validation/netplay/`:

- 1,800 stress frames, 1,791 restores, 12,537 replayed frames, zero desyncs.
- Fourteen malformed snapshots rejected without changing the live state;
  valid snapshots round-trip exactly.
- Production DLL peers over real UDP, identical final full-machine checksums
  and fighter health.
- Two real US Online VS menu routes, invitations, independent JP selection,
  native rollback combat and return from combat (the original qualification
  returned to the connection window; current builds retain the room).
- F4 remained open for 50 online frames while both peers continued and
  converged. Negotiated match-finish boundaries also converged.
- Clumsy 0.3 configured for 40 ms outbound lag and 2% outbound loss: 600 frames
  converged with about 107–108 ms measured RTT; a second run with F4 and a
  negotiated finish converged at frame 690 with about 108–109 ms RTT.

Clumsy was restricted to
`outbound and loopback and udp and (udp.DstPort == 29070 or udp.DstPort == 29071)`.
Both impaired runs completed before its 120-second automatic stop. WinDivert
was observed running. No Clumsy process remains after the tests. The driver
service can remain loaded after its capture handles close. Loopback capture
can affect a packet more than once; configured lag is not an exact measured
one-way delay. Loss is the configured probability, not an observed exact rate.
See the [Clumsy manual](https://jagt.github.io/clumsy/manual.html).

`tools/Test-ClumsyNetplay.ps1` provides an administrator-only reproducible
matrix for baseline, 40 ms/2% loss, 100 ms/5% loss and 10% loss, with scoped
filters, recorded process lifetime, automatic timeout and cleanup. Only the
40 ms/2% profile has been qualified here; the broader scripted matrix has not
been completed. `tools/verify_udp_netplay.py` also supports isolated loopback
impairment using `--net-lag-ms`, `--net-jitter-ms`, `--net-loss-percent` and
`--finish-ack-delay-ms`. These flags run a scoped UDP proxy; they do not
change system networking. `--condition-label` alone does not inject faults.

The automated checks use two processes on one Windows machine. On October 26,
2026 the user also reported a successful local-PC/Shadow-PC online smoke test,
with synchronized movement/attacks, names and live 31–32 ms ping visible.
See `validation/manual-online-smoke.json`. The exact NAT route was not
instrumented. The user also confirmed a successful rollback match between
North Carolina and Georgia (NC vs GA), and the launcher records that test under
**Ready**. This is user-reported gameplay validation; no packet trace or timing
measurements were supplied for that match. Broader Internet performance and
long-session coverage remain unverified. The port still reports missing static coverage and uses
the framework's interpreter bridge for some guest code. This is not a fully
static recompile.

`seed_online_rematch_tests` passed 64 consecutive room cycles, including stale
control/game packets, palette preservation, held-confirm input, repeated late
finish acknowledgement and explicit departure. Seven production-DLL matches
passed over loopback UDP: five using a fixed qualification frame and two using
the normal negotiated finish boundary. Both peers agreed on every final
checksum and returned to selection with the same invitation, without desyncs.
See `validation/online-rematches.json` and
`validation/online-rematches-finish.json`. These new rematch checks did not use
Clumsy; the impaired-network qualification above predates this change.

`seed_online_lobby_tests` verifies input isolation and cancellation.
`seed_online_room_tests` verifies invitations, independent selection, startup
agreement and borrowed socket lifetime. `tools/verify_online_menu.py` exercises
the full menu-to-online-to-menu path. `tools/verify_menu_modes.py` and
`tools/verify_jp_local_vs.py` guard the disconnected lobby, JP Training and
Local VS paths against regressions.

## Match startup and return to selection

The 0.1.4 fixed build synchronizes at the first initialized native battle
frame, before the round intro. The original 0.1.4 advanced 600 neutral frames
before synchronizing, which skipped the intro and part of the round timer.
The startup regression checks all 27 native fighter IDs and 25 stages and
requires round phase zero; all 52 cases initialize in at most 157 frames.

After a successful result, A/B remain guarded until both players return to
the next character-select epoch and release their controls. Previously, a
neutral input before the opponent returned could arm B and let a subsequent
battle attack close the room. A regression reproduced that failure before the
fix and now passes as part of 64 consecutive room cycles. Late, validated
result acknowledgements also refresh the returning peer's liveness.

Connection errors remain visible above the online tabs until the player
starts another action. `last-game.log` records match convergence, frame and
checksum results, and the reason for an unexpected return to connection.
This covers the reproduced input bug; other Internet disconnect causes
remain possible and should now leave a visible reason.

The fixed build passed a natural KO and a separate run with 65 ms one-way
lag, 15 ms jitter, 3% configured packet loss and a 1,200 ms extra final-ACK
delay. Both peers agreed on the final frame and checksum, without desyncs.
Two full menu-driven Quick Match rematches also retained one invitation,
including stage changes. These new impaired checks used the isolated proxy,
not Clumsy. See `validation/online-startup-rematch-fixed.json`.

The initial synchronized state changed, so both players need the fixed build.
Quick Match keeps old and fixed clients in separate compatible queues.

## Player names and title statistics

Set **Player name** in the online window before hosting or joining. Names are
saved beside the game in `player-name.txt`, support UTF-8 and are bounded to 48
bytes. Both peers receive the same player order. During online combat the
window title displays both names, the configured input delay in frames, the
most recent rollback length in frames and measured round-trip ping in ms.
The title refreshes four times per second. This is display metadata and does
not enter the deterministic game state. Both players must use this build.

## Local black outlines

The simple black fighter outline and its opacity are display effects on each PC.
Set the toggle and **Outline opacity** slider on the launcher's **Play** page;
settings apply when launching. It is not
transmitted and does not affect simulation checksums, so a compatible opponent
can use the unoutlined build. Mixed outlined/unoutlined DLLs were tested in
both host/guest arrangements with matching state after 360 frames and zero
desyncs. See [black outlines](Black-Outlines.md).

# Lacus Quick Match announcements

The existing matchmaking Worker sends genuine waiting-queue announcements to
`#quick-match` through a channel-scoped incoming webhook. No Discord application,
bot token, AI model, Gateway connection, privileged intent, slash command, or
separate matchmaking queue is required.

The first alert waits 15 seconds so instant matches and cancelled searches do
not ping the server. It mentions **Matchmaking** and the region explicitly chosen
in the game: **NA**, **SA**, **EU**, **AS**, **AF**, or **OC**. Any region mentions
only Matchmaking. There is one active announcement per selected region and game
compatibility group. Counts update by editing that message without re-pinging;
when the search ends, the message is marked closed. Notifications are limited
to one new alert per 30 seconds globally and three minutes per region/build
and per source-IP hash. Normal queue alarms update counts about every 15 seconds.

The alert never publishes IP addresses, game names chosen by players, invitation
codes, or match keys. Incompatible game builds are counted separately. Discord
cannot hold up gameplay or allocate a second opponent; notification HTTP work
runs only in the auxiliary alarm handler, outside the game and queue messages.

## Connect the channel

1. In the requested server, edit **#quick-match → Integrations → Webhooks**.
   Create an incoming webhook named **Lacus - Quick Match**, assigned to that
   channel. Creation requires the server's Manage Webhooks permission.
2. Copy its private webhook URL. Keep it out of chat, source files, issue
   comments, release files and screenshots.
3. Copy the IDs of the Matchmaking and six region roles. Role IDs are not
   credentials. Each role must be mentionable for webhook role pings to notify
   members; individual members' notification preferences still apply.
4. Generate the avatar locally with `tools/export_discord_avatar.py --rom <your
   supported JP ROM> --out <E-work-folder>/lacus-quick-match.png`. This decodes
   the original Lacus portrait (JP resource group 20, entries 88–90 by default).
   `--portrait classic`, `pink-dress`, `space-suit` or `uniform` selects another
   original shop portrait. Maps have a 16-byte header and one-based tile IDs.
   The PNG is
   private generated art and must never be committed or bundled in a portable.
5. Run `tools/Set-DiscordQueueWebhook.ps1` in PowerShell 7 with the installed
   Node/Wrangler paths if needed. It prompts privately for the webhook URL,
   verifies the requested channel, applies the Lacus avatar, prompts for role
   IDs, stores configuration through Wrangler's protected stdin, and deploys
   the Worker. No test role ping is sent by the setup script.

The helper defaults to this project's #quick-match channel ID. A fork can pass
`-ExpectedChannelId`, `-AvatarPath`, `-NodePath` and `-WranglerPath` explicitly.
Existing Workers Scripts write authorization is needed to store these secrets
and deploy; no additional Discord account access is granted to Cloudflare.

Worker secrets: `DISCORD_WEBHOOK_URL`, `DISCORD_MATCHMAKING_ROLE_ID`,
`DISCORD_REGION_ROLE_IDS` (JSON map keyed `na`, `sa`, `eu`, `as`, `af`, `oc`),
and `DISCORD_ALERTS_ENABLED=true`. Missing or invalid configuration disables
announcements without affecting matchmaking. Set the enable flag to `false`
to stop future alerts; revoke the webhook in Discord to invalidate its URL.

## Delivery and verification

Discord rate limits are honored. PATCH operations can be retried using their
known message IDs. Discord does not document an idempotency key for webhook
POSTs: after an ambiguous timeout or process restart, that search's ping is
suppressed instead of being posted twice. A notification may be missed in that
case; the real in-game queue remains accurate and playable.

The Node tests mock Discord; they never post to the server. They cover all seven
region cases, live count edits, closed searches, safe mentions, cooldowns,
reconstruction, cancellation races, rate limits and uncertain-delivery handling.
The local workerd integration checks that original queue matching, invitations
and compatibility isolation still work. A live Discord smoke test is required
after the private webhook and roles are configured.

Run `node tools/verify_discord_worker.mjs <Wrangler-tooling-directory>` to check
the native Workers fetch path with outbound requests intercepted locally. This
checks the readiness probe, initial announcement and closure edit without
posting to Discord. Workers requires a wrapped global `fetch` when it is stored
on a class, and supports `redirect: "manual"` rather than `"error"`. Redirects
are returned without following them, keeping the webhook token on Discord.

`GET /health` reports whether Discord configuration is ready, disabled, or
invalid. `GET /health/discord` also checks webhook reachability and reports the
last delivery's fixed status, method and HTTP/Discord error codes. Neither
endpoint returns credentials, message contents, webhook metadata, player names,
IP addresses or invitation codes. Readiness probes are read-only and cached
for one minute; they do not post a message or ping roles. A `not_attempted`
delivery means no queue announcement has reached the HTTP transport yet.

## GitHub release announcements

Release notices use a separate incoming webhook in `#announcments`, with the
astronaut shop portrait selected for **Lacus - Launcher Updates**. The queue
webhook retains the profile picture selected by the server owner.

The repository's GitHub webhook sends **Releases** events as `application/json`
to the existing Worker's `/v1/github/releases` endpoint. SSL verification and
Active are enabled. A shared 64-character hexadecimal signing secret is stored
in GitHub's webhook configuration and the Worker's
`GITHUB_RELEASE_WEBHOOK_SECRET` secret. The channel's private posting URL lives
in the Worker's `DISCORD_RELEASE_WEBHOOK_URL` secret. Neither credential belongs
in source or portable builds.

The Worker verifies GitHub's HMAC-SHA256 signature and repository, accepts only
published releases, then posts an ordinary Discord webhook message. This keeps
the chosen Lacus name and avatar: Discord's `/github` compatibility endpoint
overrides both with GitHub branding. Pre-releases are labelled. Notifications
use a separate durable instance, deduplicate by release ID, and send no role
mentions. A pending or uncertain POST is not automatically repeated after a
restart. Rejected requests can be redelivered from GitHub's delivery page.
There is no recurring polling job.

Manage this under **Repository Settings → Webhooks** and inspect **Recent
Deliveries** after publishing a release. Signed ping requests test the
connection without posting. Other release actions are acknowledged without
announcements.

References: [Discord webhooks](https://docs.discord.com/developers/resources/webhook),
[allowed role mentions](https://docs.discord.com/developers/resources/message#allowed-mentions-object),
[Discord rate limits](https://docs.discord.com/developers/topics/rate-limits).

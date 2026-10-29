[CmdletBinding()]
param(
    [string]$NodePath = "node",
    [string]$WranglerPath,
    [string]$ExpectedChannelId = "1565503095427961015",
    [string]$AvatarPath
)
$ErrorActionPreference = 'Stop'
if ($NodePath -eq 'node' -and -not (Get-Command node -ErrorAction SilentlyContinue)) {
    $bundledNode = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
    if (Test-Path -LiteralPath $bundledNode) { $NodePath = $bundledNode }
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$workRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\work'))
if (-not $WranglerPath) { $WranglerPath = Join-Path $workRoot 'cloudflare\tooling\node_modules\wrangler\bin\wrangler.js' }
if (-not $AvatarPath) { $AvatarPath = Join-Path $workRoot 'discord-launcher-update\lacus-quick-match.png' }
$configPath = Join-Path $repoRoot 'server\matchmaking\wrangler.toml'
$secureWebhook = Read-Host 'Paste the #quick-match webhook URL (hidden; never paste it into chat)' -AsSecureString
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureWebhook)
try {
    $webhook = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    $uri = $null
    if (-not [Uri]::TryCreate($webhook, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -ne 'https' -or $uri.Host -ne 'discord.com' -or $uri.Port -ne 443 -or
        $uri.UserInfo -or $uri.Query -or $uri.Fragment -or
        $uri.AbsolutePath -notmatch '^/api/(v10/)?webhooks/[0-9]{17,20}/[A-Za-z0-9._-]{32,256}$') {
        throw 'Use the original Discord channel webhook URL.'
    }
    try { $metadata = Invoke-RestMethod -Uri $webhook -MaximumRedirection 0 }
    catch { throw 'Discord could not verify this webhook. Check the private URL and try again.' }
    if ($metadata.channel_id -ne $ExpectedChannelId) { throw 'The webhook belongs to a different channel. Select #quick-match.' }
    $roles = @{}
    $matchmakingRole = Read-Host 'Matchmaking role ID'
    foreach ($region in @('na','sa','eu','as','af','oc')) { $roles[$region] = Read-Host ($region.ToUpperInvariant() + ' role ID') }
    $allRoles = @($matchmakingRole) + @($roles.Values)
    if (($allRoles | Where-Object { $_ -notmatch '^[0-9]{17,20}$' }).Count -or
        ($allRoles | Select-Object -Unique).Count -ne 7) { throw 'Enter seven distinct Discord role IDs.' }
    if (-not (Test-Path -LiteralPath $AvatarPath -PathType Leaf)) { throw 'Export the requested Lacus avatar first (tools/export_discord_avatar.py).' }
    $avatar = [IO.File]::ReadAllBytes($AvatarPath)
    if ($avatar.Length -gt 262144 -or $avatar.Length -lt 8 -or
        [Convert]::ToHexString($avatar[0..7]) -ne '89504E470D0A1A0A') { throw 'Use the locally exported Lacus PNG.' }
    $identity = @{name='Lacus - Quick Match';avatar=('data:image/png;base64,' + [Convert]::ToBase64String($avatar))} | ConvertTo-Json -Compress
    try { $null = Invoke-RestMethod -Method Patch -Uri $webhook -ContentType 'application/json' -Body $identity -MaximumRedirection 0 }
    catch { throw 'Discord could not apply the Lacus avatar to this webhook.' }
    $settings = @{DISCORD_ALERTS_ENABLED='true';DISCORD_WEBHOOK_URL=$webhook;
        DISCORD_MATCHMAKING_ROLE_ID=$matchmakingRole;DISCORD_REGION_ROLE_IDS=($roles | ConvertTo-Json -Compress)}
    $env:XDG_CONFIG_HOME = Join-Path $workRoot 'cloudflare\config'
    $env:XDG_CACHE_HOME = Join-Path $workRoot 'cloudflare\cache'
    $env:WRANGLER_LOG_PATH = Join-Path $workRoot 'cloudflare\logs'
    $env:WRANGLER_SEND_METRICS = 'false'
    $env:TEMP = Join-Path $workRoot 'cloudflare\tmp'
    $env:TMP = $env:TEMP
    # Secret values travel only over stdin. No token is written to a file or argv.
    $settings | ConvertTo-Json -Compress | & $NodePath $WranglerPath secret bulk --config $configPath
    if ($LASTEXITCODE -ne 0) { throw 'Cloudflare secret configuration failed.' }
    & $NodePath $WranglerPath deploy --config $configPath --keep-vars --message 'Enable queue-only Discord webhook announcements'
    if ($LASTEXITCODE -ne 0) { throw 'Worker deployment failed; the webhook secret remains private in Cloudflare.' }
    Write-Host 'Lacus Quick Match announcements are configured. Only real queued searches produce alerts.'
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    $webhook = $null; $settings = $null; $identity = $null; $metadata = $null
}

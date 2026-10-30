# Runtime-only updater. Uses Windows PowerShell/.NET; no Python or administrator rights.
param(
    [string]$Root, [string]$Archive, [string]$ExpectedHash, [string]$ExpectedVersion,
    [int]$ParentId = 0, [switch]$ValidateOnly, [switch]$NoRestart, [switch]$LibraryOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Also works when launched from a PowerShell 7 parent with a different module path.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + [IO.Path]::PathSeparator + $env:PSModulePath
function Get-UpdateHash([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function Write-UpdateJson([string]$Path, $Value) {
    $text = ConvertTo-Json -InputObject $Value -Depth 12
    [IO.File]::WriteAllText($Path, $text, [Text.UTF8Encoding]::new($false))
}
function Assert-PlainPath([string]$RootPath, [string]$Path) {
    $base = [IO.Path]::GetFullPath($RootPath).TrimEnd('\')
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -ne $base -and -not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'An update path is outside the portable folder.'
    }
    $current = $full
    while ($current.Length -ge $base.Length) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Update paths cannot contain links or junctions.'
            }
        }
        if ($current -eq $base) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    return $full
}
function Get-RuntimeNames {
    return @('GundamSeedRecomp.exe', 'GundamSeedLauncher.exe', 'SeedJpCombat.dll', 'SDL3.dll',
        'controllerdb.txt', 'launcher-settings.ini', 'START HERE.txt', 'portable-manifest.json',
        'Update-Portable.ps1', 'Haro.ico', 'LauncherShip.ico',
        'concrt140.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
        'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vccorlib140.dll',
        'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll',
        'gamefiles/Game_and_Bios_Here.file',
        'config/launcher-settings.ini', 'config/portable-manifest.json', 'config/Update-Portable.ps1',
        'resource/Haro.ico', 'resource/LauncherShip.ico', 'resource/controllerdb.txt',
        'resource/input-prompts/switch_dpad_up.png', 'resource/input-prompts/switch_dpad_down.png',
        'resource/input-prompts/switch_dpad_left.png', 'resource/input-prompts/switch_dpad_right.png',
        'resource/input-prompts/switch_button_a.png', 'resource/input-prompts/switch_button_b.png',
        'resource/input-prompts/switch_button_l.png', 'resource/input-prompts/switch_button_r.png')
}
function Assert-ArchiveName([string]$Name) {
    if (-not $Name -or $Name.Contains('\') -or $Name.StartsWith('/') -or
        $Name -match '[<>:"|?*\x00-\x1f]') { throw 'Unsafe archive filename.' }
    foreach ($part in $Name.TrimEnd('/').Split('/')) {
        if (-not $part -or $part -eq '.' -or $part -eq '..' -or $part -match '[. ]$' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
            throw 'Unsafe archive path component.'
        }
    }
}
function Expand-VerifiedPortable([string]$ZipPath, [string]$Destination, [string]$Version) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        if ($zip.Entries.Count -gt 256) { throw 'Unexpected number of archive entries.' }
        $names = @{}
        $files = @{}
        $archiveRoot = $null
        [long]$total = 0
        foreach ($entry in $zip.Entries) {
            Assert-ArchiveName $entry.FullName
            if ($names.ContainsKey($entry.FullName)) { throw 'Duplicate archive filename.' }
            $names[$entry.FullName] = $true
            if (($entry.ExternalAttributes -shr 16 -band 0xf000) -eq 0xa000) {
                throw 'Archive contains a symbolic link.'
            }
            $parts = $entry.FullName.TrimEnd('/').Split('/')
            if ($null -eq $archiveRoot) { $archiveRoot = $parts[0] }
            if ($parts[0] -cne $archiveRoot -or
                $archiveRoot -cne ('GundamSeedDestinyBattleAssaultPC-Portable-Windows-x64-' + $Version)) {
                throw 'Unexpected portable root or version.'
            }
            if ($entry.FullName.EndsWith('/')) {
                if ($parts.Count -gt 3 -or ($parts.Count -ge 2 -and $parts[1] -cnotin @('licenses','gamefiles','config','resource','Logs')) -or ($parts.Count -eq 3 -and ($parts[1] -cne 'resource' -or $parts[2] -cnotin @('licenses','input-prompts')))) {
                    throw 'Unexpected archive directory.'
                }
                continue
            }
            if ($parts.Count -lt 2) { throw 'Archive has no portable root.' }
            $relative = [string]::Join('/', $parts[1..($parts.Count - 1)])
            $license = $relative -cmatch '^(?:resource/)?licenses/[^/]+\.txt$'
            if ($relative -cnotin (Get-RuntimeNames) -and -not $license) {
                throw ('Package includes a non-runtime file: ' + $relative)
            }
            if ($entry.Length -gt 100000000 -or $entry.Length -lt 0) { throw 'Runtime file is too large.' }
            $total += $entry.Length
            if ($total -gt 536870912) { throw 'Unpacked portable is too large.' }
            $files[$relative] = $entry
        }
        $organized = $files.ContainsKey('config/portable-manifest.json')
        $manifestName = if ($organized) { 'config/portable-manifest.json' } else { 'portable-manifest.json' }
        if (-not $files.ContainsKey($manifestName)) { throw 'Portable manifest is missing.' }
        if ($organized -and $files.ContainsKey('portable-manifest.json')) { throw 'Mixed portable layouts.' }
        [IO.Directory]::CreateDirectory($Destination) | Out-Null
        foreach ($relative in $files.Keys) {
            $entry = $files[$relative]
            $target = Assert-PlainPath $Destination (Join-Path $Destination $relative)
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            $source = $entry.Open()
            $output = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
            try {
                $block = [byte[]]::new(65536)
                [long]$copied = 0
                while (($count = $source.Read($block, 0, $block.Length)) -gt 0) {
                    $copied += $count
                    if ($copied -gt $entry.Length) { throw 'Unpacked length exceeds its declared size.' }
                    $output.Write($block, 0, $count)
                }
                if ($copied -ne $entry.Length) { throw 'Incomplete archive member.' }
            } finally { $output.Dispose(); $source.Dispose() }
        }
        $manifest = [IO.File]::ReadAllText((Join-Path $Destination $manifestName)) | ConvertFrom-Json
        foreach ($key in @('roms_included','bios_included','personal_data_included','game_asset_caches_included')) {
            if ($manifest.$key -isnot [bool] -or $manifest.$key) { throw 'Package is not runtime-only.' }
        }
        if ($manifest.version -cne $Version -or $manifest.source_commit -cnotmatch '^[a-f0-9]{40}$' -or
            $manifest.platform -cne 'windows-x64' -or
            $manifest.distribution_format -cnotin @('seedpc-runtime-only-v3','seedpc-runtime-only-v4','seedpc-runtime-only-v5')) {
            throw 'Unsupported portable manifest or version.'
        }
        if ($organized -ne ($manifest.distribution_format -eq 'seedpc-runtime-only-v5')) { throw 'Portable layout does not match format.' }
        foreach ($name in $files.Keys) {
            if ($organized -and $name -in @('launcher-settings.ini','portable-manifest.json','Update-Portable.ps1','controllerdb.txt','Haro.ico','LauncherShip.ico')) { throw 'Legacy file in organized portable.' }
            if (-not $organized -and ($name.StartsWith('config/') -or $name.StartsWith('resource/'))) { throw 'Organized file in legacy portable.' }
        }
        $members = @($manifest.files.PSObject.Properties)
        if ($members.Count -ne $files.Count - 1) { throw 'Manifest has missing or extra files.' }
        foreach ($member in $members) {
            if (-not $files.ContainsKey($member.Name) -or $member.Name -eq $manifestName) {
                throw 'Manifest refers to an unexpected file.'
            }
            $path = Join-Path $Destination $member.Name
            if ($member.Value.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
                (Get-Item -LiteralPath $path).Length -ne [long]$member.Value.bytes -or
                (Get-UpdateHash $path) -cne $member.Value.sha256) {
                throw ('Runtime file verification failed: ' + $member.Name)
            }
        }
        $required = @('GundamSeedRecomp.exe','GundamSeedLauncher.exe','SeedJpCombat.dll','SDL3.dll')
        $required += if ($organized) { @('resource/controllerdb.txt','config/launcher-settings.ini','config/Update-Portable.ps1') } else { @('controllerdb.txt','launcher-settings.ini') }
        if ($organized) { $required += @((Get-RuntimeNames) | Where-Object { $_.StartsWith('resource/input-prompts/') }) }
        foreach ($name in $required) {
            if (-not $files.ContainsKey($name)) { throw ('Required runtime file is missing: ' + $name) }
        }
        if ($manifest.distribution_format -eq 'seedpc-runtime-only-v4' -and
            -not $files.ContainsKey('Update-Portable.ps1')) { throw 'New portable has no updater.' }
        return @($files.Keys)
    } finally { $zip.Dispose() }
}
function Replace-AppFile([string]$Source, [string]$Target) {
    $temporary = $Target + '.update-' + [guid]::NewGuid().ToString('N')
    try {
        [IO.File]::Copy($Source, $temporary, $false)
        if ([IO.File]::Exists($Target)) { [IO.File]::Replace($temporary, $Target, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $Target) }
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
function Restore-UpdatePlan([string]$RootPath, [string]$Backup, $Plan) {
    foreach ($item in $Plan) {
        $target = Assert-PlainPath $RootPath (Join-Path $RootPath $item.name)
        $source = Assert-PlainPath $Backup (Join-Path $Backup $item.name)
        if ($item.existed) { Replace-AppFile $source $target }
        elseif ([IO.File]::Exists($target)) { [IO.File]::Delete($target) }
    }
}
function Remove-UpdateWork([string]$RootPath, [string]$Job, [string]$Path) {
    $fullJob = Assert-PlainPath $RootPath $Job
    $full = Assert-PlainPath $RootPath $Path
    if (-not $full.StartsWith($fullJob + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Cannot clean an update path outside its job.'
    }
    if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Recurse -Force }
}
function Invoke-PortableUpdate {
    param([string]$RootPath, [string]$ZipPath, [string]$Hash, [string]$Version,
          [int]$WaitFor = 0, [switch]$CheckOnly, [switch]$SkipRestart)
    $rootPath = [IO.Path]::GetFullPath($RootPath).TrimEnd('\')
    $zipPath = Assert-PlainPath $rootPath $ZipPath
    $job = [IO.Path]::GetDirectoryName($zipPath)
    $updates = [IO.Path]::GetDirectoryName($job)
    $parsedGuid = [guid]::Empty
    if ($updates -notin @((Join-Path $rootPath '.updates'),(Join-Path $rootPath 'config/updates')) -or
        -not [guid]::TryParse([IO.Path]::GetFileName($job), [ref]$parsedGuid)) {
        throw 'Update must be staged in a unique job inside this portable folder.'
    }
    if ($Hash -cnotmatch '^[a-f0-9]{64}$' -or $Version -cnotmatch '^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$') {
        throw 'Invalid expected download digest or version.'
    }
    $payload = Join-Path $job ('payload-' + [guid]::NewGuid().ToString('N'))
    $backup = Join-Path $job 'backup'
    $journalPath = Join-Path $job 'journal.json'
    $changed = $false
    $backupReady = $false
    $success = $false
    $plan = @()
    try {
        if ((Get-Item -LiteralPath $zipPath).Length -gt 268435456 -or
            (Get-UpdateHash $zipPath) -cne $Hash) {
            throw 'The downloaded ZIP failed SHA-256 verification.'
        }
        $files = @(Expand-VerifiedPortable $zipPath $payload $Version)
        if ($CheckOnly) { return }
        if ($WaitFor -gt 0) {
            $parent = Get-Process -Id $WaitFor -ErrorAction SilentlyContinue
            if ($null -ne $parent -and -not $parent.WaitForExit(60000)) {
                throw 'The launcher did not close. No application files were replaced.'
            }
        }
        foreach ($name in $files) {
            $target = Assert-PlainPath $rootPath (Join-Path $rootPath $name)
            if ($name -in @('launcher-settings.ini','config/launcher-settings.ini','gamefiles/Game_and_Bios_Here.file') -and
                (Test-Path -LiteralPath $target)) { continue }
            if ($name -eq 'config/launcher-settings.ini' -and [IO.File]::Exists((Join-Path $rootPath 'launcher-settings.ini'))) { continue }
            if ((Test-Path -LiteralPath $target) -and -not [IO.File]::Exists($target)) {
                throw ('A directory blocks an application file: ' + $name)
            }
            $plan += [pscustomobject]@{ name=$name; existed=[IO.File]::Exists($target) }
        }
        # Check every existing runtime file for locks before replacing anything.
        foreach ($item in $plan) {
            if ($item.existed) {
                $stream = [IO.File]::Open((Join-Path $rootPath $item.name), [IO.FileMode]::Open,
                                         [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
                $stream.Dispose()
            }
        }
        foreach ($item in $plan) {
            if ($item.existed) {
                $target = Join-Path $backup $item.name
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
                [IO.File]::Copy((Join-Path $rootPath $item.name), $target, $false)
            }
        }
        $backupReady = $true
        $journal = @{ status='applying'; version=$Version; root=$rootPath; plan=$plan }
        Write-UpdateJson $journalPath $journal
        $ordered = @($plan | Sort-Object @{Expression={ $_.name.EndsWith('portable-manifest.json') }}, name)
        foreach ($item in $ordered) {
            $target = Assert-PlainPath $rootPath (Join-Path $rootPath $item.name)
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            $changed = $true
            Replace-AppFile (Join-Path $payload $item.name) $target
        }
        foreach ($item in $plan) {
            if ((Get-UpdateHash (Join-Path $payload $item.name)) -ne
                (Get-UpdateHash (Join-Path $rootPath $item.name))) {
                throw ('Installed file could not be verified: ' + $item.name)
            }
        }
        $journal.status = 'completed'
        Write-UpdateJson $journalPath $journal
        Write-UpdateJson (Join-Path $updates 'last-result.json') @{
            success=$true; version=$Version; message=('Updated to ' + $Version + '. Your settings and saves were preserved.')
        }
        $success = $true
    } catch {
        $failure = $_.Exception.Message
        if ($changed -and $backupReady) {
            try {
                Restore-UpdatePlan $rootPath $backup $plan
                if (Test-Path -LiteralPath $journalPath) {
                    $journal.status = 'rolled-back'
                    Write-UpdateJson $journalPath $journal
                }
                $failure = 'Update failed; application files were restored. ' + $failure
            } catch {
                $failure = 'Update interrupted. Recovery copies are in ' + $backup + '. ' + $_.Exception.Message
            }
        }
        if (-not $CheckOnly) {
            Write-UpdateJson (Join-Path $updates 'last-result.json') @{
                success=$false; version=$Version; message=$failure
            }
        }
        throw $failure
    } finally {
        Remove-UpdateWork $rootPath $job $payload
        if (-not $CheckOnly -and -not $SkipRestart) {
            $launcher = Join-Path $rootPath 'GundamSeedLauncher.exe'
            if ([IO.File]::Exists($launcher)) {
                Start-Process -FilePath $launcher -ArgumentList @('--page','updates') -WorkingDirectory $rootPath -WindowStyle Normal
            }
        }
    }
    if ($success) { [IO.File]::Delete($zipPath) }
}
if (-not $LibraryOnly) {
    try {
        Invoke-PortableUpdate -RootPath $Root -ZipPath $Archive -Hash $ExpectedHash -Version $ExpectedVersion `
            -WaitFor $ParentId -CheckOnly:$ValidateOnly -SkipRestart:$NoRestart
    } catch { Write-Error $_; exit 1 }
}

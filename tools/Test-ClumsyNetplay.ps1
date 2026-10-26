param(
  [Parameter(Mandatory=$true)][string]$Rom,
  [Parameter(Mandatory=$true)][string]$Bios,
  [string]$Clumsy,
  [string]$Peer,
  [string]$Python,
  [string]$Out,
  [int]$Frames=600
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$taskRoot=Split-Path -Parent (Split-Path -Parent $projectRoot)
if (!$Clumsy) { $Clumsy=Join-Path $taskRoot 'work/tools/clumsy-0.3-win64/clumsy.exe' }
if (!$Peer) { $Peer=Join-Path $taskRoot 'work/build/seed-sdl3/Release/seed_jp_online_module_peer.exe' }
if (!$Python) { $Python=(Get-Command python -ErrorAction Stop).Source }
if (!$Out) { $Out=Join-Path $taskRoot 'work/clumsy-matrix' }
$Clumsy=(Resolve-Path -LiteralPath $Clumsy).Path
$Peer=(Resolve-Path -LiteralPath $Peer).Path
$Rom=(Resolve-Path -LiteralPath $Rom).Path
$Bios=(Resolve-Path -LiteralPath $Bios).Path
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Clumsy needs its packet driver. Run this script from an administrator PowerShell after approving Windows elevation.'
}
if (Get-Process -Name clumsy -ErrorAction SilentlyContinue) { throw 'An existing Clumsy process is running. Stop it before this matrix to avoid stacking filters.' }
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$Out=(Resolve-Path -LiteralPath $Out).Path
$filter='outbound and loopback and udp and (udp.DstPort == 29070 or udp.DstPort == 29071)'
$profiles=@(
  @{name='baseline';lag=0;loss=0},
  @{name='lag40-loss2';lag=40;loss=2},
  @{name='lag100-loss5';lag=100;loss=5},
  @{name='loss10';lag=0;loss=10}
)
$matrix=[ordered]@{started_utc=[DateTime]::UtcNow.ToString('o');filter=$filter;clumsy_sha256=(Get-FileHash -LiteralPath $Clumsy -Algorithm SHA256).Hash;frames=$Frames;profiles=@();cleanup_complete=$false;success=$false}
$clumsyProcess=$null
try {
  foreach ($profile in $profiles) {
    $caseDir=Join-Path $Out $profile.name
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    $record=[ordered]@{name=$profile.name;lag_ms=$profile.lag;drop_percent=$profile.loss;started_utc=[DateTime]::UtcNow.ToString('o');clumsy_pid=$null;clumsy_active_before=$false;clumsy_active_after=$false;passed=$false}
    $matrix.profiles+=,$record
    try {
      if ($profile.name -ne 'baseline') {
        $clumsyArgs='--filter "'+$filter+'" --lag '+$(if($profile.lag){'on'}else{'off'})+' --lag-inbound off --lag-outbound on --lag-time '+$profile.lag+' --drop on --drop-inbound off --drop-outbound on --drop-chance '+$profile.loss+' --timeout 60'
        $clumsyProcess=Start-Process -FilePath $Clumsy -ArgumentList $clumsyArgs -WorkingDirectory (Split-Path -Parent $Clumsy) -WindowStyle Hidden -PassThru
        $record.clumsy_pid=$clumsyProcess.Id
        Start-Sleep -Milliseconds 1500
        if ($clumsyProcess.HasExited) { throw 'Clumsy exited before the qualification began.' }
        $record.clumsy_active_before=$true
        $record.driver=Get-CimInstance Win32_SystemDriver -Filter "Name = 'WinDivert'" | Select-Object Name,State,Started
      }
      $verification=Join-Path $PSScriptRoot 'verify_udp_netplay.py'
      & $Python $verification --exe $Peer --rom $Rom --bios $Bios --out $caseDir --frames $Frames --condition-label ('clumsy-matrix-'+$profile.name) --window --finish-fence *> (Join-Path $caseDir 'qualification.log')
      if ($LASTEXITCODE) { throw "Native qualification failed for $($profile.name). See $caseDir/qualification.log" }
      if ($clumsyProcess) {
        $record.clumsy_active_after=!$clumsyProcess.HasExited
        if (!$record.clumsy_active_after) { throw 'Clumsy stopped before the test completed; this run cannot qualify the requested condition.' }
      }
      $record.result=Get-Content -LiteralPath (Join-Path $caseDir 'report.json') -Raw | ConvertFrom-Json
      $record.passed=$true
    } finally {
      if ($clumsyProcess -and !$clumsyProcess.HasExited) {
        # Only stop the exact process created for this profile, after verifying
        # its executable. Closing it removes its WinDivert capture handles.
        $owned=Get-Process -Id $clumsyProcess.Id -ErrorAction SilentlyContinue
        if ($owned -and $owned.Path -eq $Clumsy) { Stop-Process -Id $owned.Id; $clumsyProcess.WaitForExit(5000) | Out-Null }
      }
      $record.finished_utc=[DateTime]::UtcNow.ToString('o')
      $record.filter_process_stopped=(!$clumsyProcess -or $clumsyProcess.HasExited)
      $clumsyProcess=$null
      $matrix | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Out 'matrix.json') -Encoding utf8
    }
  }
  $matrix.success=$true
} catch {
  $matrix.error=$_.Exception.Message
  $_ | Out-String | Set-Content -LiteralPath (Join-Path $Out 'error.txt')
  throw
} finally {
  $matrix.cleanup_complete=!(Get-Process -Name clumsy -ErrorAction SilentlyContinue)
  $matrix.finished_utc=[DateTime]::UtcNow.ToString('o')
  $matrix | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Out 'matrix.json') -Encoding utf8
}

param(
    [string]$Rom = 'D:/MiSTer stuff/games and bios/gba/Mobile Suit Gundam Seed - Battle Assault (USA).gba',
    [string]$Bios = "$PSScriptRoot/../../work/bios/gba_bios.bin",
    [string]$JpRom = 'D:/MiSTer stuff/games and bios/gba/Kidou Senshi Gundam - Seed Destiny (Japan).gba'
)
$ErrorActionPreference = 'Stop'
$taskWork = [IO.Path]::GetFullPath("$PSScriptRoot/../../work")
function Checked([scriptblock]$Action) {
    & $Action
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" }
}
if ((Get-FileHash -LiteralPath $Rom -Algorithm SHA256).Hash -ne 'D7952790A4B82B6D230A4953E4BFE48F6693DDD550F1CF0E7ACE74BCE95453F8') {
    throw 'Expected the verified US BGNE cartridge.'
}
if ((Get-FileHash -LiteralPath $Bios -Algorithm SHA1).Hash -ne '300C20DF6731A33952DED8C436F7F186D25D3492') {
    throw 'Expected the verified 16384-byte GBA BIOS.'
}
Checked { python "$PSScriptRoot/tools/prepare_runtime.py" --engine "$taskWork/deps/gbarecomp" }
Checked { python "$PSScriptRoot/tools/import_jp_assets.py" $JpRom --out "$taskWork/jp-assets-verified" }
Checked { cmake -S "$taskWork/deps/gbarecomp" -B "$taskWork/build/core" -G 'Visual Studio 17 2022' -A x64 -DGBARECOMP_COMPILER_CACHE=OFF }
Checked { cmake --build "$taskWork/build/core" --config Release --target gba_recompile --parallel 6 }
$taskCore = "$taskWork/build/core/Release/gba_recompile.exe"
Checked { & $taskCore --rom $Rom --config "$PSScriptRoot/game.toml" --out "$taskWork/generated/us" --max-functions 16384 }
Checked { & $taskCore --rom $JpRom --config "$PSScriptRoot/game-jp-combat.toml" --out "$taskWork/generated/jp" --max-functions 24000 }
Checked { & $taskCore --bios $Bios --config "$taskWork/deps/gbarecomp/bios/gba_bios.toml" --out "$taskWork/deps/gbarecomp/src/runtime/generated_bios" }
Checked { cmake -S $PSScriptRoot -B "$taskWork/build/seed" -G 'Visual Studio 17 2022' -A x64 }
Checked { cmake --build "$taskWork/build/seed" --config Release --target GundamSeedRecomp GundamSeedLauncher seed_jp_asset_reference --parallel 6 }
Checked { python "$PSScriptRoot/tools/capture_jp_selection.py" --reference-exe "$taskWork/build/seed/Release/seed_jp_asset_reference.exe" --rom $JpRom --bios $Bios --out "$taskWork/jp-selector-reference" }
Checked { python "$PSScriptRoot/tools/build_jp_selection.py" --rom $JpRom --assets "$taskWork/jp-assets-verified" --skin "$taskWork/jp-selector-reference/skin.rgb" --out "$taskWork/destiny-selection.bin" }
New-Item -ItemType Directory -Force "$PSScriptRoot/app/assets", "$taskWork/build/seed/Release/assets" | Out-Null
Copy-Item -LiteralPath "$taskWork/destiny-selection.bin" -Destination "$PSScriptRoot/app/assets/destiny-selection.bin"
Copy-Item -LiteralPath "$taskWork/destiny-selection.bin" -Destination "$taskWork/build/seed/Release/assets/destiny-selection.bin"
Write-Host "Baseline executable: $taskWork/build/seed/Release/GundamSeedRecomp.exe"
Write-Host 'Local VS: choose Local VS. Online VS: use VS Mode to host or paste a direct UDP invitation.'
Write-Host 'Online VS uses shareable codes/links and native AutoPunch-compatible hole punching; no fallback relay is configured.'
Write-Host 'Local VS uses JP combat and the full JP selection grid behind the US menus.'
Write-Host "Launcher: $PSScriptRoot/app/GundamSeedLauncher.exe"

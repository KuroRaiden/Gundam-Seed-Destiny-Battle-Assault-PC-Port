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
Checked { python "$PSScriptRoot/tools/import_jp_assets.py" $JpRom --out "$taskWork/jp-assets-verified" }
Checked { cmake -S "$taskWork/deps/gbarecomp" -B "$taskWork/build/core" -G 'Visual Studio 17 2022' -A x64 -DGBARECOMP_COMPILER_CACHE=OFF }
Checked { cmake --build "$taskWork/build/core" --config Release --target gba_recompile --parallel 6 }
$taskCore = "$taskWork/build/core/Release/gba_recompile.exe"
Checked { & $taskCore --rom $Rom --config "$PSScriptRoot/game.toml" --out "$taskWork/generated/us" --max-functions 16384 }
Checked { & $taskCore --bios $Bios --config "$taskWork/deps/gbarecomp/bios/gba_bios.toml" --out "$taskWork/deps/gbarecomp/src/runtime/generated_bios" }
Checked { cmake -S $PSScriptRoot -B "$taskWork/build/seed" -G 'Visual Studio 17 2022' -A x64 }
Checked { cmake --build "$taskWork/build/seed" --config Release --target GundamSeedRecomp --parallel 6 }
Write-Host "Baseline executable: $taskWork/build/seed/Release/GundamSeedRecomp.exe"
Write-Host 'Local VS, rollback, NAT traversal and join links still require integration.'
Write-Host 'Japanese UI resources are imported; roster and screen integration are not complete.'

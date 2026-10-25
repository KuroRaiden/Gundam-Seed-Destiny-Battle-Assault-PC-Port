param([string]$JpRom = 'D:/MiSTer stuff/games and bios/gba/Kidou Senshi Gundam - Seed Destiny (Japan).gba')
$ErrorActionPreference = 'Stop'
$taskWork = [IO.Path]::GetFullPath("$PSScriptRoot/../../work")
python "$PSScriptRoot/tools/import_jp_assets.py" $JpRom --out "$taskWork/jp-assets-verified"
if ($LASTEXITCODE -ne 0) { throw 'Japanese ROM asset import failed.' }
cmake -S $PSScriptRoot -B "$taskWork/build/seed"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build "$taskWork/build/seed" --config Release --target seed_jp_asset_oracle --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Japanese asset oracle build failed.' }
& "$taskWork/build/seed/Release/seed_jp_asset_oracle.exe" $JpRom "$taskWork/jp-assets-verified"
if ($LASTEXITCODE -ne 0) { throw 'Imported assets differ from the original Japanese decoder output.' }

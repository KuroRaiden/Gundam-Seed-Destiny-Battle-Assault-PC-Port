param([string]$Rom = 'D:/MiSTer stuff/games and bios/gba/Mobile Suit Gundam Seed - Battle Assault (USA).gba')
$ErrorActionPreference = 'Stop'
$taskWork = [IO.Path]::GetFullPath("$PSScriptRoot/../../work")
if ((Get-FileHash -LiteralPath $Rom -Algorithm SHA256).Hash -ne 'D7952790A4B82B6D230A4953E4BFE48F6693DDD550F1CF0E7ACE74BCE95453F8') {
    throw 'Expected the verified US BGNE cartridge.'
}
cmake --build "$taskWork/build/seed" --config Release --target seed_input_oracle --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Input oracle build failed.' }
& "$taskWork/build/seed/Release/seed_input_oracle.exe" $Rom
if ($LASTEXITCODE -ne 0) { throw 'Native input differs from the US cartridge.' }

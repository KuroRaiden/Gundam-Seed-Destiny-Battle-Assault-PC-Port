$ErrorActionPreference = 'Stop'
$taskWork = [IO.Path]::GetFullPath("$PSScriptRoot/../../work")
cmake -S $PSScriptRoot -B "$taskWork/build/launcher" -G 'Visual Studio 17 2022' -A x64 -DSEED_BUILD_GAME=OFF
if ($LASTEXITCODE -ne 0) { throw 'Launcher configuration failed.' }
cmake --build "$taskWork/build/launcher" --config Release --target GundamSeedLauncher seed_launcher_tests --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
Write-Host "Launcher: $PSScriptRoot/app/GundamSeedLauncher.exe"
Write-Host 'The game host is built separately with build.ps1.'

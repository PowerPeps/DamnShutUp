$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$build = Join-Path $root 'build'
$dir = Join-Path $env:LOCALAPPDATA 'Programs\DamnShutUp'
$exe = Join-Path $dir 'DamnShutUp.exe'

cmake -S $root -B $build -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$running = Get-Process DamnShutUp -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force
    $running | Wait-Process
}

New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item (Join-Path $build 'Release\DamnShutUp.exe') $exe -Force
Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'DamnShutUp' -Value "`"$exe`""
Start-Process $exe

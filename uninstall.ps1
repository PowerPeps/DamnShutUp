$ErrorActionPreference = 'Stop'

$running = Get-Process DamnShutUp -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force
    $running | Wait-Process
}

Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'DamnShutUp' -ErrorAction SilentlyContinue
Remove-Item 'HKCU:\Software\DamnShutUp' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:LOCALAPPDATA 'Programs\DamnShutUp') -Recurse -Force -ErrorAction SilentlyContinue
# Data folder of versions storing mute.txt
Remove-Item (Join-Path $env:LOCALAPPDATA 'DamnShutUp') -Recurse -Force -ErrorAction SilentlyContinue

param(
    [string]$Source = (Join-Path (Split-Path -Parent $PSScriptRoot) 'SteamFiles\DELTARUNE'),
    [string]$Output = (Join-Path (Split-Path -Parent $PSScriptRoot) 'data\prepared\deltarune\deltarunevita')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$translation = Join-Path $root 'mods\PTBR'
$borderSource = Join-Path $root 'data\Deltarune Borders (Console Version)'

if (-not (Test-Path (Join-Path $Source 'data.win'))) {
    throw "Arquivos Windows nao encontrados em: $Source"
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

$launcher = Join-Path $Output 'chapter0'
New-Item -ItemType Directory -Force -Path $launcher | Out-Null
Copy-Item -Force (Join-Path $Source 'data.win') (Join-Path $launcher 'game.droid')
if (Test-Path (Join-Path $Source 'options.ini')) {
    Copy-Item -Force (Join-Path $Source 'options.ini') $launcher
}

for ($chapter = 1; $chapter -le 5; $chapter++) {
    $from = Join-Path $Source "chapter${chapter}_windows"
    $to = Join-Path $Output "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Get-ChildItem -Force $from | Where-Object Name -ne 'data.win' | Copy-Item -Destination $to -Recurse -Force
    Copy-Item -Force (Join-Path $from 'data.win') (Join-Path $to 'game.droid')
}

$music = Join-Path $Output 'music'
New-Item -ItemType Directory -Force -Path $music | Out-Null
Copy-Item -Force (Join-Path $Source 'mus\*') $music

if (Test-Path -LiteralPath $borderSource) {
    $borders = Join-Path $Output 'borders'
    New-Item -ItemType Directory -Force -Path $borders | Out-Null
    Get-ChildItem -LiteralPath $borderSource -File -Filter '*.png' | Copy-Item -Destination $borders -Force
}

$mods = Join-Path $Output 'mods\PTBR'
New-Item -ItemType Directory -Force -Path $mods | Out-Null
if (Test-Path (Join-Path $translation 'data.win')) {
    $modLauncher = Join-Path $mods 'chapter0'
    New-Item -ItemType Directory -Force -Path $modLauncher | Out-Null
    Copy-Item -Force (Join-Path $translation 'data.win') (Join-Path $modLauncher 'game.droid')
}
for ($chapter = 1; $chapter -le 5; $chapter++) {
    $from = Join-Path $translation "chapter$chapter"
    if (-not (Test-Path $from)) { continue }
    $to = Join-Path $mods "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Get-ChildItem -Force $from | Where-Object Name -ne 'data.win' | Copy-Item -Destination $to -Recurse -Force
    if (Test-Path (Join-Path $from 'data.win')) {
        Copy-Item -Force (Join-Path $from 'data.win') (Join-Path $to 'game.droid')
    }
}
if (Test-Path (Join-Path $translation 'mus')) {
    $modMusic = Join-Path $mods 'music'
    New-Item -ItemType Directory -Force -Path $modMusic | Out-Null
    Copy-Item -Force (Join-Path $translation 'mus\*') $modMusic
}

Write-Host "Windows data prepared at: $Output"

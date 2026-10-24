param(
    [string]$Source = (Join-Path (Split-Path -Parent $PSScriptRoot) 'SteamFiles\DELTARUNE'),
    [string]$Output = (Join-Path (Split-Path -Parent $PSScriptRoot) 'data\prepared\deltarune\deltarunevita')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$borderSource = Join-Path $root 'data\Deltarune Borders (Console Version)'
$pvrSource = Join-Path $root 'data\pvr'

if (-not (Test-Path (Join-Path $Source 'data.win'))) {
    throw "Arquivos Windows nao encontrados em: $Source"
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null

$launcher = Join-Path $Output 'chapter0'
New-Item -ItemType Directory -Force -Path $launcher | Out-Null
Copy-Item -Force (Join-Path $Source 'data.win') $launcher
$umtCli = Join-Path $root 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$chapter0VersionScript = Join-Path $root 'tools\SetChapter0BuildVersion.csx'
if ((Test-Path -LiteralPath $umtCli) -and (Test-Path -LiteralPath $chapter0VersionScript)) {
    $chapter0Data = Join-Path $launcher 'data.win'
    $chapter0Versioned = Join-Path $launcher 'data.versioned.win'
    & $umtCli load $chapter0Data -s $chapter0VersionScript -o $chapter0Versioned
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao aplicar a versao da build no Chapter 0."
    }
    Move-Item -Force -LiteralPath $chapter0Versioned -Destination $chapter0Data
}
if (Test-Path (Join-Path $Source 'options.ini')) {
    Copy-Item -Force (Join-Path $Source 'options.ini') $launcher
}

for ($chapter = 1; $chapter -le 5; $chapter++) {
    $from = Join-Path $Source "chapter${chapter}_windows"
    $to = Join-Path $Output "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Get-ChildItem -Force $from | Copy-Item -Destination $to -Recurse -Force
}

$rollercoasterPatch = Join-Path $root 'tools\PatchChapter2Rollercoaster.csx'
$chapter2Data = Join-Path $Output 'chapter2\data.win'
if ((Test-Path -LiteralPath $umtCli) -and (Test-Path -LiteralPath $rollercoasterPatch) -and
    (Test-Path -LiteralPath $chapter2Data)) {
    $patchedData = Join-Path $Output 'chapter2\data.rollercoaster.win'
    & $umtCli load $chapter2Data -s $rollercoasterPatch -o $patchedData
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao otimizar o rollercoaster do Chapter 2.' }
    Move-Item -Force -LiteralPath $patchedData -Destination $chapter2Data
}

$music = Join-Path $Output 'music'
New-Item -ItemType Directory -Force -Path $music | Out-Null
Copy-Item -Force (Join-Path $Source 'mus\*') $music

if (Test-Path -LiteralPath $borderSource) {
    $borders = Join-Path $Output 'borders'
    New-Item -ItemType Directory -Force -Path $borders | Out-Null
    Get-ChildItem -LiteralPath $borderSource -File -Filter '*.png' | Copy-Item -Destination $borders -Force
}

# GPU-ready PVRTC2 texture set. The Vita runner uses these pages first and
# falls back only to the prepared RGBA4444 texture-cache, never to runtime
# pixel decoding from data.win.
if (Test-Path -LiteralPath $pvrSource) {
    $pvrOutput = Join-Path $Output 'pvr'
    New-Item -ItemType Directory -Force -Path $pvrOutput | Out-Null
    Get-ChildItem -LiteralPath $pvrSource -Directory -Filter 'chapter*' |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $pvrOutput -Recurse -Force
        }
}

Write-Host "Windows data prepared at: $Output"

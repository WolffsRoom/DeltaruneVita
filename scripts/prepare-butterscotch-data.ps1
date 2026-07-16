param(
    [string]$SourceAssets,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SourceAssets) { $SourceAssets = Join-Path $root 'data\extracted-apk\assets' }
if (-not $Destination) { $Destination = Join-Path $root 'data\prepared\deltarune\butterscotch' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

for ($chapter = 0; $chapter -le 5; $chapter++) {
    $wad = Join-Path $SourceAssets "chapter$chapter.wad"
    if (-not (Test-Path -LiteralPath $wad)) {
        throw "Arquivo ausente: $wad"
    }

    $target = Join-Path $Destination "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($wad)
    try {
        $entry = $archive.GetEntry('assets/game.droid')
        if ($null -eq $entry) { throw "assets/game.droid ausente em $wad" }
        $output = Join-Path $target 'game.droid'
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $output, $true)
        Write-Host "Capitulo $chapter -> $output ($($entry.Length) bytes)"
    }
    finally {
        $archive.Dispose()
    }
}

Write-Host "Copie a pasta 'butterscotch' para ux0:data/deltarune/ no Vita."

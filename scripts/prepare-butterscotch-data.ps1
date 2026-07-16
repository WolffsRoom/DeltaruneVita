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

$musicSource = Join-Path $SourceAssets 'mus'
$musicTarget = Join-Path $Destination 'music'
if (Test-Path -LiteralPath $musicSource) {
    New-Item -ItemType Directory -Force -Path $musicTarget | Out-Null
    Copy-Item -Force -Path (Join-Path $musicSource '*') -Destination $musicTarget
    Write-Host "Musicas compartilhadas -> $musicTarget"
}

for ($chapter = 0; $chapter -le 5; $chapter++) {
    $wad = Join-Path $SourceAssets "chapter$chapter.wad"
    if (-not (Test-Path -LiteralPath $wad)) {
        throw "Arquivo ausente: $wad"
    }

    $target = Join-Path $Destination "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($wad)
    try {
        $gameEntry = $archive.GetEntry('assets/game.droid')
        if ($null -eq $gameEntry) { throw "assets/game.droid ausente em $wad" }
        foreach ($entry in $archive.Entries) {
            if (-not $entry.FullName.StartsWith('assets/') -or [string]::IsNullOrEmpty($entry.Name)) { continue }
            $relative = $entry.FullName.Substring(7).Replace('/', [IO.Path]::DirectorySeparatorChar)
            $output = Join-Path $target $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $output, $true)
        }
        Write-Host "Capitulo $chapter -> dados e audio extraidos"
    }
    finally {
        $archive.Dispose()
    }
}

Write-Host "Copie a pasta 'butterscotch' para ux0:data/deltarune/ no Vita."

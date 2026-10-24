param(
    [string]$DataRoot = (Join-Path $PSScriptRoot '..\data')
)

$ErrorActionPreference = 'Stop'

$dataRootResolved = (Resolve-Path -LiteralPath $DataRoot).Path
$preparedRoot = Join-Path $dataRootResolved 'prepared\deltarune\deltarunevita'

foreach ($name in @('pvr', 'texture-cache')) {
    $source = Join-Path $dataRootResolved $name
    $destination = Join-Path $preparedRoot $name

    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing render asset directory: $source"
    }

    # Mirror rather than merge. A stale PVR left by an older policy can take
    # priority over a newly selected RGBA4444 page on the Vita.
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    if ($name -eq 'pvr') {
        # Original profiles remain global. Translation profiles are routed to
        # their own language directory because translated data.win files can
        # repack every texture-page index.
        Get-ChildItem -LiteralPath $source -Force | Where-Object {
            $_.Name -notmatch '-ptbr$'
        } | Copy-Item -Destination $destination -Recurse -Force

        $ptbrPvr = Join-Path (Split-Path -Parent $dataRootResolved) 'mods\Lang\Portuguese-BR\pvr'
        New-Item -ItemType Directory -Force -Path $ptbrPvr | Out-Null
        Get-ChildItem -LiteralPath $source -Directory -Filter '*-ptbr' | ForEach-Object {
            $chapterName = $_.Name -replace '-ptbr$', ''
            $target = Join-Path $ptbrPvr $chapterName
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
            Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force
        }
    } else {
        Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination $destination -Recurse -Force
    }

    $count = @(Get-ChildItem -LiteralPath $destination -File -Recurse).Count
    Write-Host "$name synchronized: $count files"
}

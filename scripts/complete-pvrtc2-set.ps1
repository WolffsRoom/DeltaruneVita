param(
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int]$MaxParallel = 6,
    [switch]$RefreshExports,
    [switch]$ExportOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'data\pvr-work\complete'
$output = Join-Path $root 'data\pvr'
$prepared = Join-Path $root 'data\prepared\deltarune\deltarunevita'
$umtCli = Join-Path $root 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$exportScript = Join-Path $root 'tools\ExportEmbeddedTexturesForPvr.csx'
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path -LiteralPath $PvrTexTool)) { throw "PVRTexToolCLI not found: $PvrTexTool" }

function Get-DataWin([int]$Chapter, [bool]$PtBr) {
    if ($PtBr) {
        return Join-Path $root "mods\Lang\Portuguese-BR\chapter$Chapter\data.win"
    }
    if ($Chapter -eq 0) { return Join-Path $prepared 'chapter0\data.win' }
    return Join-Path $prepared "chapter$Chapter\data.win"
}

function Export-TexturePages([int]$Chapter, [bool]$PtBr) {
    $dataWin = Get-DataWin $Chapter $PtBr
    if (-not (Test-Path -LiteralPath $dataWin)) { return $null }
    $variant = if ($PtBr) { "chapter$Chapter-ptbr" } else { "chapter$Chapter" }
    $target = Join-Path $work $variant
    if ($RefreshExports -or -not (Test-Path -LiteralPath $target)) {
        if (-not (Test-Path -LiteralPath $umtCli)) { throw "UndertaleModCli not found: $umtCli" }
        Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        $env:DELTARUNE_PVR_EXPORT = $target
        & $umtCli load $dataWin -s $exportScript | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Texture export failed: $dataWin" }
    }
    return $target
}

function Test-ExactPvrtc2([string]$Pvr, [string]$Png) {
    if (-not (Test-Path -LiteralPath $Pvr)) { return $false }
    $bytes = [IO.File]::ReadAllBytes($Pvr)
    if ($bytes.Length -lt 52 -or [BitConverter]::ToUInt64($bytes, 8) -ne 5 -or
        [BitConverter]::ToUInt32($bytes, 16) -ne 1) { return $false }
    $image = [Drawing.Image]::FromFile($Png)
    try {
        return [BitConverter]::ToUInt32($bytes, 28) -eq $image.Width -and
               [BitConverter]::ToUInt32($bytes, 24) -eq $image.Height
    } finally { $image.Dispose() }
}

function Test-Pvrtc2Eligible([string]$Png) {
    $image = [Drawing.Image]::FromFile($Png)
    try {
        # PVRTCII has a minimum encoded block footprint. Tiny lookup strips
        # such as Chapter 5 page 16 (128x1) cannot be represented by
        # PVRTexTool and must stay in the lossless RGBA4444 cache.
        return $image.Width -ge 8 -and $image.Height -ge 8
    } finally { $image.Dispose() }
}

$items = [Collections.Generic.List[object]]::new()
foreach ($ptbr in @($false, $true)) {
    for ($chapter = 0; $chapter -le 5; ++$chapter) {
        $sourceDir = Export-TexturePages $chapter $ptbr
        if ($null -eq $sourceDir) { continue }
        $targetName = if ($ptbr) { "chapter$chapter-ptbr" } else { "chapter$chapter" }
        Get-ChildItem -LiteralPath $sourceDir -Filter 'Texture *.png' | Where-Object { -not $_.PSIsContainer } | ForEach-Object {
            if ($_.BaseName -notmatch '^Texture (\d+)$') { return }
            $page = [int]$Matches[1]
            $items.Add([pscustomobject]@{
                Source = $_.FullName
                Target = Join-Path $output ("{0}\page_{1:D3}.pvr" -f $targetName, $page)
                Variant = $targetName
                Page = $page
            })
        }
    }
}

$unsupported = @($items | Where-Object { -not (Test-Pvrtc2Eligible $_.Source) })
foreach ($item in $unsupported) {
    Remove-Item -LiteralPath $item.Target -Force -ErrorAction SilentlyContinue
    $marker = [IO.Path]::ChangeExtension($item.Target, '.rgba4444')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $marker) | Out-Null
    Set-Content -LiteralPath $marker -Value 'PVRTC2 unsupported dimensions; use texture-cache RGBA4444.' -Encoding ASCII
}
$eligible = @($items | Where-Object { Test-Pvrtc2Eligible $_.Source })
$pending = @($eligible | Where-Object { -not (Test-ExactPvrtc2 $_.Target $_.Source) })
Write-Host "PVRTC2 sRGB BEST scan: pending=$($pending.Count) existing=$($eligible.Count - $pending.Count) unsupported=$($unsupported.Count) total=$($items.Count)"
if ($ExportOnly) {
    Write-Host 'Export-only mode: source atlas set is ready; conversion was not started.'
    exit 0
}
$running = [Collections.Generic.List[object]]::new()
$done = 0
foreach ($item in $pending) {
    while ($running.Count -ge $MaxParallel) {
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if (-not $running[$i].Process.HasExited) { continue }
            if ($running[$i].Process.ExitCode -ne 0) {
                throw "PVRTC conversion failed: $($running[$i].Target)"
            }
            $running.RemoveAt($i)
            $done++
            Write-Progress -Activity 'PVRTC2 sRGB BEST' -Status "$done / $($pending.Count)" -PercentComplete (100 * $done / [Math]::Max(1, $pending.Count))
        }
        if ($running.Count -ge $MaxParallel) { Start-Sleep -Milliseconds 200 }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $item.Target) | Out-Null
    $args = @('-i', ('"{0}"' -f $item.Source), '-o', ('"{0}"' -f $item.Target),
              '-f', 'PVRTCII_4BPP,UBN,sRGB', '-q', 'PVRTCBEST')
    $process = Start-Process -FilePath $PvrTexTool -ArgumentList $args -PassThru -WindowStyle Hidden
    $running.Add([pscustomobject]@{ Process = $process; Target = $item.Target })
}

foreach ($entry in $running) {
    $entry.Process.WaitForExit()
    if ($entry.Process.ExitCode -ne 0) { throw "PVRTC conversion failed: $($entry.Target)" }
    $done++
}
Write-Progress -Activity 'PVRTC2 sRGB BEST' -Completed
Write-Host "PVRTC2 complete: converted=$done existing=$($items.Count - $pending.Count) total=$($items.Count)"

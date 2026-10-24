param(
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int]$MaxParallel = 2,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $PvrTexTool -PathType Leaf)) {
    throw "PVRTexToolCLI not found: $PvrTexTool"
}

$variants = @(
    @{ Name = 'chapter2'; Source = Join-Path $root 'data\pvr-work\complete\chapter2' },
    @{ Name = 'chapter2-ptbr'; Source = Join-Path $root 'data\pvr-work\complete\chapter2-ptbr' }
)
$items = [Collections.Generic.List[object]]::new()
foreach ($variant in $variants) {
    if (-not (Test-Path -LiteralPath $variant.Source -PathType Container)) {
        throw "Missing exported atlas directory: $($variant.Source)"
    }
    $destination = Join-Path $root ("data\pvr\" + $variant.Name)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $variant.Source -Filter 'Texture *.png' | ForEach-Object {
        if ($_.BaseName -notmatch '^Texture (\d+)$') { return }
        $page = [int]$Matches[1]
        $target = Join-Path $destination ('page_{0:D3}.bc3.pvr' -f $page)
        if ($Force -or -not (Test-Path -LiteralPath $target -PathType Leaf)) {
            $items.Add([pscustomobject]@{ Source=$_.FullName; Target=$target })
        }
    }
}

Write-Host "Chapter 2 BC3 scan: pending=$($items.Count)"
$running = [Collections.Generic.List[object]]::new()
$done = 0
foreach ($item in $items) {
    while ($running.Count -ge [Math]::Max(1, $MaxParallel)) {
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if (-not $running[$i].Process.HasExited) { continue }
            if ($running[$i].Process.ExitCode -ne 0) { throw "BC3 conversion failed: $($running[$i].Target)" }
            $running.RemoveAt($i)
            $done++
        }
        if ($running.Count -ge [Math]::Max(1, $MaxParallel)) { Start-Sleep -Milliseconds 150 }
    }
    $args = @('-ics','sRGB','-f','BC3,UB,sRGB','-i',('"{0}"' -f $item.Source),'-o',('"{0}"' -f $item.Target))
    $process = Start-Process -FilePath $PvrTexTool -ArgumentList $args -PassThru -WindowStyle Hidden
    $running.Add([pscustomobject]@{ Process=$process; Target=$item.Target })
}
foreach ($entry in $running) {
    $entry.Process.WaitForExit()
    if ($entry.Process.ExitCode -ne 0) { throw "BC3 conversion failed: $($entry.Target)" }
    $done++
}

# Refuse stale BC3 pages produced from an older/downscaled export. Atlas UVs
# are defined in data.win and require exactly the same dimensions as source.
Add-Type -AssemblyName System.Drawing
foreach ($variant in $variants) {
    $destination = Join-Path $root ("data\pvr\" + $variant.Name)
    Get-ChildItem -LiteralPath $variant.Source -Filter 'Texture *.png' -File | ForEach-Object {
        if ($_.BaseName -notmatch '^Texture (\d+)$') { return }
        $target = Join-Path $destination ('page_{0:D3}.bc3.pvr' -f [int]$Matches[1])
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { return }
        $sourceImage = [Drawing.Image]::FromFile($_.FullName)
        try {
            $bytes = [IO.File]::ReadAllBytes($target)
            $height = [BitConverter]::ToUInt32($bytes, 24)
            $width = [BitConverter]::ToUInt32($bytes, 28)
            if ($width -ne $sourceImage.Width -or $height -ne $sourceImage.Height) {
                throw "BC3 dimension mismatch: $target is ${width}x${height}, source is $($sourceImage.Width)x$($sourceImage.Height). Re-run with -Force."
            }
        } finally {
            $sourceImage.Dispose()
        }
    }
}
Write-Host "Chapter 2 BC3 complete: converted=$done"

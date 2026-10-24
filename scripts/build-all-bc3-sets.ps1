param(
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int]$MaxParallel = 2,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $root 'data\pvr-work\complete'
$destinationRoot = Join-Path $root 'data\pvr'

if (-not (Test-Path -LiteralPath $PvrTexTool -PathType Leaf)) {
    throw "PVRTexToolCLI not found: $PvrTexTool"
}

$variants = @(
    'chapter0', 'chapter0-ptbr',
    'chapter1', 'chapter1-ptbr',
    'chapter2', 'chapter2-ptbr',
    'chapter3', 'chapter3-ptbr',
    'chapter4', 'chapter4-ptbr',
    'chapter5'
)

$items = [Collections.Generic.List[object]]::new()
$expected = @{}
foreach ($variant in $variants) {
    $source = Join-Path $sourceRoot $variant
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing exported atlas directory: $source"
    }
    $destination = Join-Path $destinationRoot $variant
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $pages = @(Get-ChildItem -LiteralPath $source -Filter 'Texture *.png' -File)
    $expected[$variant] = $pages.Count
    foreach ($pageFile in $pages) {
        if ($pageFile.BaseName -notmatch '^Texture (\d+)$') { continue }
        $page = [int]$Matches[1]
        $target = Join-Path $destination ('page_{0:D3}.bc3.pvr' -f $page)
        if ($Force -or -not (Test-Path -LiteralPath $target -PathType Leaf)) {
            $items.Add([pscustomobject]@{
                Variant = $variant
                Page = $page
                Source = $pageFile.FullName
                Target = $target
            })
        }
    }
}

Write-Host "All-chapter BC3 scan: pending=$($items.Count) total=$((($expected.Values | Measure-Object -Sum).Sum))"
$running = [Collections.Generic.List[object]]::new()
$done = 0
foreach ($item in $items) {
    while ($running.Count -ge [Math]::Max(1, $MaxParallel)) {
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if (-not $running[$i].Process.HasExited) { continue }
            if ($running[$i].Process.ExitCode -ne 0) {
                throw "BC3 conversion failed: $($running[$i].Target)"
            }
            $running.RemoveAt($i)
            $done++
        }
        if ($running.Count -ge [Math]::Max(1, $MaxParallel)) {
            Start-Sleep -Milliseconds 150
        }
    }
    $arguments = @(
        '-ics', 'sRGB',
        '-f', 'BC3,UB,sRGB',
        '-i', ('"{0}"' -f $item.Source),
        '-o', ('"{0}"' -f $item.Target)
    )
    $process = Start-Process -FilePath $PvrTexTool -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $running.Add([pscustomobject]@{ Process = $process; Target = $item.Target })
}
foreach ($entry in $running) {
    $entry.Process.WaitForExit()
    if ($entry.Process.ExitCode -ne 0) {
        throw "BC3 conversion failed: $($entry.Target)"
    }
    $done++
}

$invalid = [Collections.Generic.List[string]]::new()
foreach ($variant in $variants) {
    $destination = Join-Path $destinationRoot $variant
    $files = @(Get-ChildItem -LiteralPath $destination -Filter '*.bc3.pvr' -File)
    if ($files.Count -ne $expected[$variant]) {
        $invalid.Add("$variant count=$($files.Count) expected=$($expected[$variant])")
    }
    foreach ($file in $files) {
        $page = [int]([IO.Path]::GetFileNameWithoutExtension(
            [IO.Path]::GetFileNameWithoutExtension($file.Name)
        ) -replace '^page_', '')
        $sourceFile = Join-Path $sourceRoot (Join-Path $variant ("Texture {0}.png" -f $page))
        $stream = [IO.File]::OpenRead($file.FullName)
        try {
            $reader = [IO.BinaryReader]::new($stream)
            $magic = $reader.ReadUInt32()
            [void]$reader.ReadUInt32()
            $format = $reader.ReadUInt64()
            $colorSpace = $reader.ReadUInt32()
            [void]$reader.ReadUInt32()
            $height = $reader.ReadUInt32()
            $width = $reader.ReadUInt32()
            if ($magic -ne 0x03525650 -or $format -ne 11 -or $colorSpace -ne 1 -or
                $width -eq 0 -or $height -eq 0) {
                $invalid.Add("$variant/$($file.Name) invalid PVR3 BC3 sRGB header")
            }
            if (Test-Path -LiteralPath $sourceFile -PathType Leaf) {
                Add-Type -AssemblyName System.Drawing
                $sourceImage = [Drawing.Image]::FromFile($sourceFile)
                try {
                    if ($width -ne $sourceImage.Width -or $height -ne $sourceImage.Height) {
                        $invalid.Add("$variant/$($file.Name) dimensions=${width}x${height} source=$($sourceImage.Width)x$($sourceImage.Height)")
                    }
                } finally {
                    $sourceImage.Dispose()
                }
            }
        } finally {
            $stream.Dispose()
        }
    }
}
if ($invalid.Count -gt 0) {
    throw "BC3 validation failed:`n$($invalid -join "`n")"
}

Write-Host "All-chapter BC3 complete: converted=$done validated=$((($expected.Values | Measure-Object -Sum).Sum))"
foreach ($variant in $variants) {
    Write-Host ("  {0}: {1} pages" -f $variant, $expected[$variant])
}

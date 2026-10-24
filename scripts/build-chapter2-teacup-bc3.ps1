param(
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pages = @(6, 12, 15, 17, 19, 21)
$variants = @(
    @{ Name = 'chapter2'; Source = Join-Path $root 'data\pvr-work\chapter2-2048'; Prefix = 'page_'; TextureNames = $false },
    @{ Name = 'chapter2-ptbr'; Source = Join-Path $root 'data\pvr-work\chapter2-ptbr-current-export'; Prefix = 'Texture '; TextureNames = $true }
)

if (-not (Test-Path -LiteralPath $PvrTexTool -PathType Leaf)) {
    throw "PVRTexToolCLI not found: $PvrTexTool"
}

foreach ($variant in $variants) {
    $destination = Join-Path $root ("data\pvr\" + $variant.Name)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($page in $pages) {
        $sourceName = if ($variant.TextureNames) {
            "Texture $page.png"
        } else {
            'page_{0:D3}.png' -f $page
        }
        $source = Join-Path $variant.Source $sourceName
        $target = Join-Path $destination ('page_{0:D3}.bc3.pvr' -f $page)
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Missing source atlas: $source"
        }
        & $PvrTexTool -ics sRGB -f 'BC3,UB,sRGB' -i $source -o $target
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "BC3 conversion failed: $target"
        }
        $bytes = (Get-Item -LiteralPath $target).Length
        Write-Host "$($variant.Name) page=$page format=BC3/DXT5 bytes=$bytes"
    }
}

Write-Host 'Chapter 2 teacup BC3 test set generated.'

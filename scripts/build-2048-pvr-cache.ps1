param(
    [string]$Python = 'C:\Users\wolff\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int[]]$Chapters = (0..5),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path $root 'data\pvr-work'
$outputRoot = Join-Path $root 'data\pvr'

if (-not (Test-Path -LiteralPath $Python)) {
    throw "Python not found: $Python"
}
$excluded = @{}

# PVR files are always native GPU-ready PVRTC2. RGBA4444 belongs only to
# texture-cache and remains the fallback when a PVR is missing or rejected.
$pvrtc2Format = [UInt64]5
$profileRows = [System.Collections.Generic.List[object]]::new()

function Test-Pvrtc2Pvr([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $header = [System.IO.File]::ReadAllBytes($Path)
    if ($header.Length -lt 52) { return $false }
    $format = [BitConverter]::ToUInt64($header, 8)
    $colorSpace = [BitConverter]::ToUInt32($header, 16)
    $height = [BitConverter]::ToUInt32($header, 24)
    $width = [BitConverter]::ToUInt32($header, 28)
    $metadata = [BitConverter]::ToUInt32($header, 48)
    $expectedLength = 52L + [int64]$metadata + [int64]$width * [int64]$height / 2L
    return $format -eq $pvrtc2Format -and $colorSpace -eq 1 -and
           $width -gt 0 -and $height -gt 0 -and $header.Length -eq $expectedLength
}

$converted = 0
$existing = 0
$skipped = 0
foreach ($chapter in $Chapters) {
    $sourceDir = Join-Path $workRoot "chapter$chapter-2048"
    if (-not (Test-Path -LiteralPath $sourceDir)) { continue }
    $outputDir = Join-Path $outputRoot "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

    foreach ($png in Get-ChildItem -LiteralPath $sourceDir -File -Filter 'page_*.png') {
        if ($png.BaseName -notmatch '^page_(\d+)$') { continue }
        $page = [int]$Matches[1]
        if ($excluded.ContainsKey($chapter) -and $page -in $excluded[$chapter]) {
            $skipped++
            continue
        }

        $target = Join-Path $outputDir ($png.BaseName + '.pvr')
        if ((Test-Path -LiteralPath $target) -and -not $Force) {
            $header = [System.IO.File]::ReadAllBytes($target)
            $isSrgb = $header.Length -ge 20 -and [BitConverter]::ToUInt32($header, 16) -eq 1
            $pixelFormat = if ($header.Length -ge 16) { [BitConverter]::ToUInt64($header, 8) } else { [UInt64]::MaxValue }
            $width = if ($header.Length -ge 32) { [BitConverter]::ToUInt32($header, 28) } else { 0 }
            $height = if ($header.Length -ge 28) { [BitConverter]::ToUInt32($header, 24) } else { 0 }
            $metadata = if ($header.Length -ge 52) { [BitConverter]::ToUInt32($header, 48) } else { 0 }
            $expectedLength = 52L + [int64]$metadata + [int64]$width * [int64]$height / 2L
            if ($pixelFormat -eq $pvrtc2Format -and $isSrgb -and $header.Length -eq $expectedLength) {
                $existing++
                continue
            }
        }

        & $PvrTexTool -i $png.FullName -o $target -f PVRTCII_4BPP,UBN,sRGB -q PVRTCBEST
        if ($LASTEXITCODE -ne 0) { throw "PVRTC encoder failed: $($png.FullName)" }
        $formatName = 'PVRTC2_4BPP_sRGB_BEST'
        $alphaPercent = ''
        $profileRows.Add([pscustomobject]@{
            Chapter = $chapter
            Page = $page
            Format = $formatName
            IntermediateAlphaVisiblePercent = $alphaPercent
            Bytes = (Get-Item -LiteralPath $target).Length
        })
        $converted++
    }
}

# Modded data.win variants only need pages whose embedded atlas differs from
# the official chapter. Keep this explicit so a stale BC3/PVRTC file can never
# survive merely because the normal 2048 export directory has no counterpart.
$variantPages = @(
    @{ Source = 'chapter2-export\EmbeddedTextures\Texture 26.png'; Target = 'chapter2\page_026.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 16.png'; Target = 'chapter2-ptbr\page_016.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 17.png'; Target = 'chapter2-ptbr\page_017.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 18.png'; Target = 'chapter2-ptbr\page_018.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 19.png'; Target = 'chapter2-ptbr\page_019.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 20.png'; Target = 'chapter2-ptbr\page_020.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 21.png'; Target = 'chapter2-ptbr\page_021.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 22.png'; Target = 'chapter2-ptbr\page_022.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 23.png'; Target = 'chapter2-ptbr\page_023.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 24.png'; Target = 'chapter2-ptbr\page_024.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 25.png'; Target = 'chapter2-ptbr\page_025.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 26.png'; Target = 'chapter2-ptbr\page_026.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 27.png'; Target = 'chapter2-ptbr\page_027.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 28.png'; Target = 'chapter2-ptbr\page_028.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 29.png'; Target = 'chapter2-ptbr\page_029.pvr'; Chapter = 2 },
    @{ Source = 'chapter2-ptbr-current-export\Texture 30.png'; Target = 'chapter2-ptbr\page_030.pvr'; Chapter = 2 },
    @{ Source = 'chapter3-ptbr-export\Texture 13.png'; Target = 'chapter3-ptbr\page_013.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 14.png'; Target = 'chapter3-ptbr\page_014.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 15.png'; Target = 'chapter3-ptbr\page_015.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 16.png'; Target = 'chapter3-ptbr\page_016.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 17.png'; Target = 'chapter3-ptbr\page_017.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 18.png'; Target = 'chapter3-ptbr\page_018.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 19.png'; Target = 'chapter3-ptbr\page_019.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 20.png'; Target = 'chapter3-ptbr\page_020.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 21.png'; Target = 'chapter3-ptbr\page_021.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 22.png'; Target = 'chapter3-ptbr\page_022.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 23.png'; Target = 'chapter3-ptbr\page_023.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 24.png'; Target = 'chapter3-ptbr\page_024.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 25.png'; Target = 'chapter3-ptbr\page_025.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 26.png'; Target = 'chapter3-ptbr\page_026.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 27.png'; Target = 'chapter3-ptbr\page_027.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 28.png'; Target = 'chapter3-ptbr\page_028.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 29.png'; Target = 'chapter3-ptbr\page_029.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 33.png'; Target = 'chapter3-ptbr\page_033.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 34.png'; Target = 'chapter3-ptbr\page_034.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 35.png'; Target = 'chapter3-ptbr\page_035.pvr'; Chapter = 3 },
    @{ Source = 'chapter3-ptbr-export\Texture 36.png'; Target = 'chapter3-ptbr\page_036.pvr'; Chapter = 3 }
)
foreach ($entry in $variantPages) {
    if ($entry.Chapter -notin $Chapters) { continue }
    $source = Join-Path $workRoot $entry.Source
    $target = Join-Path $outputRoot $entry.Target
    if (-not (Test-Path -LiteralPath $source)) { throw "Variant source not found: $source" }
    if (-not $Force -and (Test-Pvrtc2Pvr $target)) {
        $existing++
        continue
    }
    $targetDir = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    & $PvrTexTool -i $source -o $target -f PVRTCII_4BPP,UBN,sRGB -q PVRTCBEST
    if ($LASTEXITCODE -ne 0) { throw "PVRTC encoder failed: $source" }
    $profileRows.Add([pscustomobject]@{
        Chapter = "$($entry.Chapter)-variant"
        Page = [int]([regex]::Match($entry.Target, 'page_(\d+)').Groups[1].Value)
        Format = 'PVRTC2_4BPP_sRGB_BEST'
        IntermediateAlphaVisiblePercent = ''
        Bytes = (Get-Item -LiteralPath $target).Length
    })
    $converted++
}

# Chapter 1 and Chapter 4's PT-BR data.win files got their whole texture atlas
# repacked by the mod build, not just the translated pages (confirmed by
# hashing every exported page against the official chapter: all of them
# differ). Unlike chapter2/3 above there is no shared subset worth curating,
# so convert every exported page instead of hand-picking a list.
$fullVariantChapters = @{ 1 = 'chapter1-ptbr-export'; 4 = 'chapter4-ptbr-export' }
foreach ($chapter in $fullVariantChapters.Keys) {
    if ($chapter -notin $Chapters) { continue }
    $sourceDir = Join-Path $workRoot $fullVariantChapters[$chapter]
    if (-not (Test-Path -LiteralPath $sourceDir)) { continue }
    $outputDir = Join-Path $outputRoot "chapter$chapter-ptbr"
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    foreach ($png in Get-ChildItem -LiteralPath $sourceDir -File -Filter 'Texture *.png') {
        if ($png.BaseName -notmatch '^Texture (\d+)$') { continue }
        $page = [int]$Matches[1]
        $target = Join-Path $outputDir ('page_{0:D3}.pvr' -f $page)
        if (-not $Force -and (Test-Pvrtc2Pvr $target)) {
            $existing++
            continue
        }
        & $PvrTexTool -i $png.FullName -o $target -f PVRTCII_4BPP,UBN,sRGB -q PVRTCBEST
        if ($LASTEXITCODE -ne 0) { throw "PVRTC encoder failed: $($png.FullName)" }
        $profileRows.Add([pscustomobject]@{
            Chapter = "$chapter-ptbr"
            Page = $page
            Format = 'PVRTC2_4BPP_sRGB_BEST'
            IntermediateAlphaVisiblePercent = ''
            Bytes = (Get-Item -LiteralPath $target).Length
        })
        $converted++
    }
}

$reportPath = Join-Path $outputRoot 'pvrtc2-profile.csv'
$preservedRows = @()
if (Test-Path -LiteralPath $reportPath) {
    $chapterKeys = @($Chapters | ForEach-Object { [string]$_; "$_-variant" })
    $preservedRows = @(Import-Csv -LiteralPath $reportPath | Where-Object { $_.Chapter -notin $chapterKeys })
}
@($preservedRows) + @($profileRows) | Sort-Object Chapter, @{ Expression = { [int]$_.Page } } |
    Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding UTF8
Write-Host "PVR converted=$converted existing=$existing excluded=$skipped profile=pvrtc2-only report=$reportPath"

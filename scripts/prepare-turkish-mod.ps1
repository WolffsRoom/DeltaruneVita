param(
    [string]$Source = 'C:\Users\wolff\Documents\SDKVita\DeltaruneVita\mods\Lang\Turkish',
    [string]$Destination,
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int]$MaxParallel = 2,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Drawing
if (-not $Destination) {
    $Destination = Join-Path $root 'mods\Lang\Turkish'
}
$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $root 'mods\Lang'))
if (-not $destinationPath.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination outside canonical language root: $destinationPath"
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) { throw "Turkish source missing: $sourcePath" }
if (-not (Test-Path -LiteralPath $PvrTexTool -PathType Leaf)) { throw "PVRTexToolCLI missing: $PvrTexTool" }

$keep = @(
    @{ Src='chapter1_windows\data.win'; Dest='chapter1\data.win' },
    @{ Src='chapter1_windows\lang\lang_tr_ch1.json'; Dest='chapter1\lang\lang_tr_ch1.json' },
    @{ Src='chapter2_windows\data.win'; Dest='chapter2\data.win' },
    @{ Src='chapter2_windows\lang\lang_ja.json'; Dest='chapter2\lang\lang_ja.json' },
    @{ Src='chapter3_windows\data.win'; Dest='chapter3\data.win' },
    @{ Src='chapter3_windows\lang\lang_en.json'; Dest='chapter3\lang\lang_en.json' },
    @{ Src='chapter4_windows\data.win'; Dest='chapter4\data.win' },
    @{ Src='chapter4_windows\lang\lang_en.json'; Dest='chapter4\lang\lang_en.json' },
    @{ Src='mus\dontforget.ogg'; Dest='music\dontforget.ogg' },
    @{ Src='vid\tennaIntroF1_compressed_28.mp4'; Dest='chapter3\vid\tennaIntroF1_compressed_28.mp4' }
)

New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
foreach ($item in $keep) {
    $sourceFile = Join-Path $sourcePath $item.Src
    if (Test-Path -LiteralPath $sourceFile -PathType Leaf) {
        $targetFile = Join-Path $destinationPath $item.Dest
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null
        Copy-Item -LiteralPath $sourceFile -Destination $targetFile -Force
    } else {
        Write-Host "Notice: Turkish file missing, skipping: $($item.Src)"
    }
}

$umt = Join-Path $root 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$exportScript = Join-Path $root 'tools\ExportEmbeddedTexturesForPvr.csx'
$policyScript = Join-Path $root 'tools\ReportVitaTexturePolicy.csx'
$workRoot = Join-Path $root 'data\pvr-work\turkish'
$pvrRoot = Join-Path $destinationPath 'pvr'
$items = [Collections.Generic.List[object]]::new()
$expected = @{}

for ($chapter = 0; $chapter -le 5; ++$chapter) {
    $dataWin = Join-Path $destinationPath "chapter$chapter\data.win"
    if (-not (Test-Path -LiteralPath $dataWin -PathType Leaf)) { continue }
    $work = Join-Path $workRoot "chapter$chapter"
    $target = Join-Path $pvrRoot "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $work,$target | Out-Null
    if ($Force -or -not (Get-ChildItem -LiteralPath $work -Filter 'Texture *.png' -File -ErrorAction SilentlyContinue)) {
        $env:DELTARUNE_PVR_EXPORT = $work
        "exit" | & $umt load $dataWin -s $exportScript
        if ($LASTEXITCODE -ne 0) { throw "Turkish texture export failed: chapter$chapter" }
    }
    $pages = @(Get-ChildItem -LiteralPath $work -Filter 'Texture *.png' -File)
    $thinPages = [Collections.Generic.List[int]]::new()
    $expected[$chapter] = 0
    foreach ($page in $pages) {
        if ($page.BaseName -notmatch '^Texture (\d+)$') { continue }
        $id = [int]$Matches[1]
        $output = Join-Path $target ('page_{0:D3}.bc3.pvr' -f $id)
        $image = [Drawing.Image]::FromFile($page.FullName)
        try {
            if ($image.Width -lt 4 -or $image.Height -lt 4) {
                $thinPages.Add($id)
                if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Force }
                continue
            }
        } finally {
            $image.Dispose()
        }
        $expected[$chapter]++
        if ($Force -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
            $items.Add([pscustomobject]@{ Source=$page.FullName; Target=$output })
        }
    }
    $policy = Join-Path $workRoot "policy-chapter$chapter.txt"
    $env:DELTARUNE_TEXTURE_POLICY = $policy
    "exit" | & $umt load $dataWin -s $policyScript
    if ($LASTEXITCODE -ne 0) { throw "Turkish texture policy failed: chapter$chapter" }
    Get-ChildItem -LiteralPath $target -Filter '*.rgba4444' -File -ErrorAction SilentlyContinue | Remove-Item -Force
    foreach ($line in Get-Content -LiteralPath $policy) {
        if ($line -match '^(\d+)\|rgba4444\|(.+)$') {
            Set-Content -LiteralPath (Join-Path $target ('page_{0:D3}.rgba4444' -f [int]$Matches[1])) -Value $Matches[2] -Encoding ASCII
        }
    }
    foreach ($id in $thinPages) {
        Set-Content -LiteralPath (Join-Path $target ('page_{0:D3}.rgba4444' -f $id)) -Value 'BC3 block dimensions unsupported; use RGBA4444.' -Encoding ASCII
    }
}

Write-Host "Turkish BC3 scan: pending=$($items.Count) total=$((($expected.Values | Measure-Object -Sum).Sum))"
$running = [Collections.Generic.List[object]]::new()
foreach ($item in $items) {
    while ($running.Count -ge [Math]::Max(1, $MaxParallel)) {
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if (-not $running[$i].Process.HasExited) { continue }
            if ($running[$i].Process.ExitCode -ne 0) { throw "Turkish BC3 conversion failed: $($running[$i].Target)" }
            $running.RemoveAt($i)
        }
        if ($running.Count -ge [Math]::Max(1, $MaxParallel)) { Start-Sleep -Milliseconds 150 }
    }
    $arguments = @('-ics','sRGB','-f','BC3,UB,sRGB','-i',$item.Source,'-o',$item.Target)
    $process = Start-Process -FilePath $PvrTexTool -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $running.Add([pscustomobject]@{ Process=$process; Target=$item.Target })
}
foreach ($entry in $running) {
    $entry.Process.WaitForExit()
    if ($entry.Process.ExitCode -ne 0) { throw "Turkish BC3 conversion failed: $($entry.Target)" }
}

for ($chapter = 0; $chapter -le 5; ++$chapter) {
    if (-not $expected.ContainsKey($chapter)) { continue }
    $actual = @(Get-ChildItem -LiteralPath (Join-Path $pvrRoot "chapter$chapter") -Filter '*.bc3.pvr' -File -ErrorAction SilentlyContinue).Count
    if ($actual -ne $expected[$chapter]) { throw "Turkish chapter$chapter BC3 count=$actual expected=$($expected[$chapter])" }
}
Write-Host "Turkish language overlay and private PVR profile prepared: $destinationPath"

param(
    [string]$Source = 'C:\Users\wolff\Downloads\deltarune_esp\deltarune\deltarunevita',
    [string]$Destination,
    [string]$PvrTexTool = 'C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe',
    [int]$MaxParallel = 2,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Drawing
if (-not $Destination) {
    $Destination = Join-Path $root 'mods\Lang\Spanish'
}
$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $root 'mods\Lang'))
if (-not $destinationPath.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination outside canonical language root: $destinationPath"
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) { throw "Spanish source missing: $sourcePath" }
if (-not (Test-Path -LiteralPath $PvrTexTool -PathType Leaf)) { throw "PVRTexToolCLI missing: $PvrTexTool" }

$keep = @(
    'chapter0\data.win',
    'chapter1\data.win', 'chapter1\lang\lang_en.json',
    'chapter1\snd_joker_ha0_es.ogg', 'chapter1\snd_joker_ha1_es.ogg',
    'chapter1\snd_joker_laugh0_es.ogg', 'chapter1\snd_joker_laugh1_es.ogg',
    'chapter2\data.win', 'chapter2\lang\lang_alt_names.json', 'chapter2\lang\lang_en.json',
    'chapter3\data.win', 'chapter3\lang\lang_alt_names.json', 'chapter3\lang\lang_en.json',
    'chapter3\vid\tennaesp.mp4',
    'chapter4\data.win', 'chapter4\lang\lang_alt_names.json', 'chapter4\lang\lang_en.json',
    'chapter5\data.win', 'chapter5\lang\lang_alt_names.json', 'chapter5\lang\lang_en.json',
    'chapter5\vid\ch5_intro_en.mp4',
    'music\ch2_creditses.ogg', 'music\credits_es_song.ogg',
    'music\dontforgetes.ogg', 'music\pink_es.ogg'
)

New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
foreach ($relative in $keep) {
    $sourceFile = Join-Path $sourcePath $relative
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) { throw "Required Spanish file missing: $relative" }
    $targetFile = Join-Path $destinationPath $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $targetFile -Force
}

$umt = Join-Path $root 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$exportScript = Join-Path $root 'tools\ExportEmbeddedTexturesForPvr.csx'
$policyScript = Join-Path $root 'tools\ReportVitaTexturePolicy.csx'
$workRoot = Join-Path $root 'data\pvr-work\spanish'
$pvrRoot = Join-Path $destinationPath 'pvr'
$items = [Collections.Generic.List[object]]::new()
$expected = @{}

for ($chapter = 0; $chapter -le 5; ++$chapter) {
    $dataWin = Join-Path $destinationPath "chapter$chapter\data.win"
    $work = Join-Path $workRoot "chapter$chapter"
    $target = Join-Path $pvrRoot "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $work,$target | Out-Null
    if ($Force -or -not (Get-ChildItem -LiteralPath $work -Filter 'Texture *.png' -File -ErrorAction SilentlyContinue)) {
        $env:DELTARUNE_PVR_EXPORT = $work
        "exit" | & $umt load $dataWin -s $exportScript | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Spanish texture export failed: chapter$chapter" }
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
    "exit" | & $umt load $dataWin -s $policyScript | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Spanish texture policy failed: chapter$chapter" }
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

Write-Host "Spanish BC3 scan: pending=$($items.Count) total=$((($expected.Values | Measure-Object -Sum).Sum))"
$running = [Collections.Generic.List[object]]::new()
foreach ($item in $items) {
    while ($running.Count -ge [Math]::Max(1, $MaxParallel)) {
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if (-not $running[$i].Process.HasExited) { continue }
            if ($running[$i].Process.ExitCode -ne 0) { throw "Spanish BC3 conversion failed: $($running[$i].Target)" }
            $running.RemoveAt($i)
        }
        if ($running.Count -ge [Math]::Max(1, $MaxParallel)) { Start-Sleep -Milliseconds 150 }
    }
    $arguments = @('-ics','sRGB','-f','BC3,UB,sRGB','-i',('"{0}"' -f $item.Source),'-o',('"{0}"' -f $item.Target))
    $process = Start-Process -FilePath $PvrTexTool -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $running.Add([pscustomobject]@{ Process=$process; Target=$item.Target })
}
foreach ($entry in $running) {
    $entry.Process.WaitForExit()
    if ($entry.Process.ExitCode -ne 0) { throw "Spanish BC3 conversion failed: $($entry.Target)" }
}

for ($chapter = 0; $chapter -le 5; ++$chapter) {
    $actual = @(Get-ChildItem -LiteralPath (Join-Path $pvrRoot "chapter$chapter") -Filter '*.bc3.pvr' -File).Count
    if ($actual -ne $expected[$chapter]) { throw "Spanish chapter$chapter BC3 count=$actual expected=$($expected[$chapter])" }
}
Write-Host "Spanish language overlay and private PVR profile prepared: $destinationPath"

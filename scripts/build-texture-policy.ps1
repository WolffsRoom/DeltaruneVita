param(
    [string]$UmtCli = (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\UndertaleModToolCLI\UndertaleModCli.exe'),
    [switch]$KeepDuplicateCaches
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$prepared = Join-Path $root 'data\prepared\deltarune\deltarunevita'
$pvrRoot = Join-Path $root 'data\pvr'
$cacheRoot = Join-Path $root 'data\texture-cache'
$policyScript = Join-Path $root 'tools\ReportVitaTexturePolicy.csx'
$report = [Collections.Generic.List[object]]::new()

if (-not (Test-Path -LiteralPath $UmtCli)) { throw "UndertaleModCli not found: $UmtCli" }

$variants = [Collections.Generic.List[object]]::new()
for ($chapter = 0; $chapter -le 5; ++$chapter) {
    $variants.Add([pscustomobject]@{ Name = "chapter$chapter"; Data = Join-Path $prepared "chapter$chapter\data.win" })
    $modData = Join-Path $root "mods\Lang\Portuguese-BR\chapter$chapter\data.win"
    if (Test-Path -LiteralPath $modData) {
        $variants.Add([pscustomobject]@{ Name = "chapter$chapter-ptbr"; Data = $modData })
    }
}

foreach ($variant in $variants) {
    if (-not (Test-Path -LiteralPath $variant.Data)) { continue }
    $policyTemp = Join-Path $root "data\pvr-work\policy-$($variant.Name).txt"
    $env:DELTARUNE_TEXTURE_POLICY = $policyTemp
    & $UmtCli load $variant.Data -s $policyScript
    if ($LASTEXITCODE -ne 0) { throw "Texture policy analysis failed: $($variant.Name)" }

    $pvrDir = Join-Path $pvrRoot $variant.Name
    $cacheDir = Join-Path $cacheRoot $variant.Name
    New-Item -ItemType Directory -Force $pvrDir,$cacheDir | Out-Null
    Get-ChildItem -LiteralPath $pvrDir -Filter '*.rgba4444' -File -ErrorAction SilentlyContinue |
        Remove-Item -Force

    $lossless = @{}
    foreach ($line in Get-Content -LiteralPath $policyTemp) {
        if ($line -notmatch '^(\d+)\|rgba4444\|(.+)$') { continue }
        $page = [int]$Matches[1]
        $reason = $Matches[2]
        $lossless[$page] = $reason
        $marker = Join-Path $pvrDir ('page_{0:D3}.rgba4444' -f $page)
        Set-Content -LiteralPath $marker -Value $reason -Encoding ASCII
        $pvr = Join-Path $pvrDir ('page_{0:D3}.pvr' -f $page)
        if (Test-Path -LiteralPath $pvr) { Remove-Item -LiteralPath $pvr -Force }
        $cache = Join-Path $cacheDir ('page_{0:D3}.r444' -f $page)
        if (-not (Test-Path -LiteralPath $cache)) {
            # The runner's chapter loading pass builds policy-selected caches
            # from the user's own data.win. Record it instead of leaving a
            # conflicting PVR in place or aborting the desktop package.
            $report.Add([pscustomobject]@{ Variant=$variant.Name; Page=$page; Format='RGBA4444_RUNTIME_BUILD'; Reason=$reason })
            continue
        }
        $report.Add([pscustomobject]@{ Variant=$variant.Name; Page=$page; Format='RGBA4444'; Reason=$reason })
    }

    foreach ($pvr in Get-ChildItem -LiteralPath $pvrDir -Filter 'page_*.pvr' -File) {
        if ($pvr.BaseName -notmatch '^page_(\d+)$') { continue }
        $page = [int]$Matches[1]
        if ($lossless.ContainsKey($page)) { continue }
        if (-not $KeepDuplicateCaches) {
            $cache = Join-Path $cacheDir ('page_{0:D3}.r444' -f $page)
            if (Test-Path -LiteralPath $cache) { Remove-Item -LiteralPath $cache -Force }
        }
        $report.Add([pscustomobject]@{ Variant=$variant.Name; Page=$page; Format='PVRTC2'; Reason='scenery_or_safe_atlas' })
    }
}
Remove-Item Env:DELTARUNE_TEXTURE_POLICY -ErrorAction SilentlyContinue
$reportPath = Join-Path $pvrRoot 'texture-policy.csv'
$report | Sort-Object Variant,Page | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding UTF8
Write-Host "Texture policy generated: $reportPath ($($report.Count) classified pages)"

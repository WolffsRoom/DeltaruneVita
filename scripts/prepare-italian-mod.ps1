param(
    [string]$Source = (Join-Path (Split-Path -Parent $PSScriptRoot) 'mods\Lang\Italian'),
    [string]$Destination,
    [switch]$SkipDownload,
    [switch]$SkipRebuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sourcePath = [IO.Path]::GetFullPath($Source)
$languageRoot = [IO.Path]::GetFullPath((Join-Path $root 'mods\Lang'))
$defaultDestination = Join-Path $root 'mods\Lang\Italian'
if (-not $Destination) { $Destination = $defaultDestination }
$destinationPath = [IO.Path]::GetFullPath($Destination)
$preparedLanguageRoot = [IO.Path]::GetFullPath((Join-Path $root 'mods\Lang'))

if (-not $sourcePath.StartsWith($languageRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Italian source outside mods/Lang: $sourcePath"
}
if (-not $destinationPath.StartsWith($preparedLanguageRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination outside canonical mods/Lang: $destinationPath"
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
    throw "Italian source missing: $sourcePath"
}

# Deltranslate 5.x data.win files are the executable translation loaders. The
# DELTARUNE (Original) directory is only the unpatched PC game and is never
# copied to the Vita package.
$downloadRoot = Join-Path $root 'data\deltarune_builder\rebuild_data_win_v1_6_bundle\rebuild\_downloads\Deltranslate-latest'
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
if (-not $SkipDownload) {
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) { throw 'GitHub CLI (gh) is required to download Deltranslate data.win files.' }
    & $gh.Source release download latest --repo Lazy-Desman/DeltranslatePatch `
        --pattern 'data_ch*.win' --dir $downloadRoot --clobber
    if ($LASTEXITCODE -ne 0) { throw 'Deltranslate download failed.' }
}

# Deltranslate resolves ../lang/ from every chapter directory. Keep the pack
# under Italian/lang and the six runners under Italian/chapterN/data.win.
$packRoot = Join-Path $sourcePath 'lang'
New-Item -ItemType Directory -Force -Path $packRoot | Out-Null
foreach ($name in @('settings.json', 'changes.json', 'strings.json')) {
    $input = Join-Path $sourcePath $name
    if (Test-Path -LiteralPath $input -PathType Leaf) {
        Copy-Item -LiteralPath $input -Destination (Join-Path $packRoot $name) -Force
    }
}
foreach ($name in @('fonts', 'chapter1', 'chapter2', 'chapter3', 'chapter4', 'chapter5')) {
    $input = Join-Path $sourcePath $name
    if (-not (Test-Path -LiteralPath $input -PathType Container)) { continue }
    Copy-Item -LiteralPath $input -Destination $packRoot -Recurse -Force
}

for ($chapter = 0; $chapter -le 5; ++$chapter) {
    $download = Join-Path $downloadRoot "data_ch$chapter.win"
    if (-not (Test-Path -LiteralPath $download -PathType Leaf)) {
        throw "Deltranslate loader missing: $download"
    }
    $chapterRoot = Join-Path $sourcePath "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $chapterRoot | Out-Null
    Copy-Item -LiteralPath $download -Destination (Join-Path $chapterRoot 'data.win') -Force
}

$builder = Join-Path $root 'data\deltarune_builder\rebuild_data_win_v1_6_bundle\source\build_language_mod.py'
$arguments = @($builder, 'Italian')
if ($SkipRebuild) { $arguments += '--skip-rebuild' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Italian rebuild/texture preparation failed ($LASTEXITCODE)." }

$ready = Join-Path $root 'data\deltarune_builder\rebuild_data_win_v1_6_bundle\rebuild\_language_ready\Italian'
if (-not (Test-Path -LiteralPath $ready -PathType Container)) {
    throw "Prepared Italian output missing: $ready"
}
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
Copy-Item -Path (Join-Path $ready '*') -Destination $destinationPath -Recurse -Force

Write-Host "ITALIAN_SOURCE=$sourcePath"
Write-Host "ITALIAN_READY=$ready"
Write-Host "ITALIAN_CANONICAL=$destinationPath"

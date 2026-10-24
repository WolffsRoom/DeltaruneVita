param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$CleanVitaGL
)

$ErrorActionPreference = 'Stop'
$rootPath = Split-Path -Parent $PSScriptRoot
$project = Join-Path $rootPath 'src\vita-probe'
$root = $rootPath.Replace('\', '/')
$vitaGlClean = if ($CleanVitaGL) { 'make -C /project/third_party/vitaGL-nosplash clean && ' } else { '' }
$nanoVgGxm = Join-Path $rootPath 'scratch\refs\nanovg-gxm'
if (-not (Test-Path -LiteralPath (Join-Path $nanoVgGxm 'src\nanovg_gxm.h'))) {
    throw "Checkout NanoVG-GXM ausente: $nanoVgGxm. Execute scripts/build-native-gxm-probe.ps1 uma vez para preparar a dependencia."
}

# A versao-base do projeto tem uma unica fonte. A sequencia interna continua
# automatica e e calculada antes da compilacao para ser embutida no eboot.
$versionFile = Join-Path $rootPath 'config\version.txt'
if (-not (Test-Path -LiteralPath $versionFile)) { throw "Arquivo de versao ausente: $versionFile" }
$baseVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if ($baseVersion -notmatch '^\d+\.\d+$') { throw "Versao invalida em config/version.txt: '$baseVersion'" }
$escapedBaseVersion = [regex]::Escape($baseVersion)

$backupDir = Join-Path $rootPath 'artifacts\Backups'
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
$currentDir = Join-Path $rootPath 'artifacts\current'
New-Item -ItemType Directory -Force -Path $currentDir | Out-Null
$existingSequences = Get-ChildItem -Path $backupDir -Filter "v$baseVersion-*" | ForEach-Object {
    if ($_.Name -match "^v$escapedBaseVersion-(\d+)") { [int]$Matches[1] }
}
$latestCurrentSequences = Get-ChildItem -Path $currentDir -Filter "Deltarune-v$baseVersion-*.vpk" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.BaseName -match "^Deltarune-v$escapedBaseVersion-(\d+)") { [int]$Matches[1] }
}
$maxSequence = @($existingSequences) + @($latestCurrentSequences) | Measure-Object -Maximum
$buildSeq = if ($null -eq $maxSequence.Maximum) { 1 } else { [int]$maxSequence.Maximum + 1 }
$versionTag = "v$baseVersion-$buildSeq"

# Keep the launcher label in the packaged data.win synchronized with the same
# version source used by the eboot and VPK name.  Preparing the game data is
# not necessarily repeated before every build, so doing this here prevents a
# new package from silently retaining the previous release label.
$chapter0Data = Join-Path $rootPath 'data\prepared\deltarune\deltarunevita\chapter0\data.win'
$chapter0Versioned = Join-Path $rootPath 'data\prepared\deltarune\deltarunevita\chapter0\data.versioned.win'
$umtCli = Join-Path $rootPath 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$chapter0VersionScript = Join-Path $rootPath 'tools\SetChapter0BuildVersion.csx'
if (-not (Test-Path -LiteralPath $chapter0Data)) { throw "Chapter 0 ausente: $chapter0Data" }
if (-not (Test-Path -LiteralPath $umtCli)) { throw "UndertaleModCli ausente: $umtCli" }
if (-not (Test-Path -LiteralPath $chapter0VersionScript)) { throw "Script de versao ausente: $chapter0VersionScript" }
Remove-Item -LiteralPath $chapter0Versioned -Force -ErrorAction SilentlyContinue
& $umtCli load $chapter0Data -s $chapter0VersionScript -o $chapter0Versioned
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $chapter0Versioned)) {
    throw 'Falha ao sincronizar a versao do Chapter 0.'
}
Move-Item -Force -LiteralPath $chapter0Versioned -Destination $chapter0Data

docker run --rm -v "${root}:/project" -w /project/src/vita-probe `
    atamanenko/vitasdk-softfp:latest sh -lc `
    "cmake -S /project/experiments/native-gxm -B /project/experiments/native-gxm/build-vita -DCMAKE_BUILD_TYPE=$Configuration -DNANOVG_GXM_SOURCE_DIR=/project/scratch/refs/nanovg-gxm -DGXM_UNIFIED_BUILD=ON -DPORT_BUILD_VERSION=$versionTag && cmake --build /project/experiments/native-gxm/build-vita -j2 --target gxm.bin-self && ${vitaGlClean}make -C /project/third_party/vitaGL-nosplash -j2 SOFTFP_ABI=1 NO_SPLASHSCREEN=1 NO_DEBUG=1 SINGLE_THREADED_GC=1 SHARED_RENDERTARGETS=1 HAVE_SHADER_CACHE=1 && cmake -S . -B build-vita -DCMAKE_BUILD_TYPE=$Configuration -DPORT_BUILD_VERSION=$versionTag -DGXM_SELF_PATH=/project/experiments/native-gxm/build-vita/gxm.bin && cmake --build build-vita -j2"

if ($LASTEXITCODE -ne 0) { throw "Build falhou com codigo $LASTEXITCODE." }

$output = Join-Path $rootPath "artifacts\current\Deltarune-$versionTag.vpk"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null

$builtVpk = Join-Path $project 'build-vita\Deltarune.vpk'
# Write-Host "Limpando NP_COMMUNICATION_ID do VPK..."
# python (Join-Path $rootPath 'scripts\remove_np_comm.py') $builtVpk

Copy-Item -Force $builtVpk $output

# Backup automatizado numerado dos arquivos do projeto
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$targetFolder = Join-Path $backupDir "$versionTag`_$timestamp"
New-Item -ItemType Directory -Force -Path $targetFolder | Out-Null
Copy-Item -Recurse -Force (Join-Path $rootPath 'src') (Join-Path $targetFolder 'src')
Copy-Item -Recurse -Force (Join-Path $rootPath 'scripts') (Join-Path $targetFolder 'scripts')
Write-Host "Backup criado em: $targetFolder"

# Cada build recebe um snapshot completo. Ao fechar um bloco de dez builds,
# removemos apenas os intermediarios daquele bloco e mantemos o marco x10.
# Nomes listados em artifacts/Backups/keep.txt nunca sao removidos.
if (($buildSeq % 10) -eq 0) {
    $keepFile = Join-Path $backupDir 'keep.txt'
    $protected = @{}
    if (Test-Path -LiteralPath $keepFile) {
        Get-Content -LiteralPath $keepFile | ForEach-Object {
            $name = $_.Trim()
            if ($name -and -not $name.StartsWith('#')) { $protected[$name] = $true }
        }
    }

    $blockStart = $buildSeq - 9
    $obsolete = Get-ChildItem -LiteralPath $backupDir -Directory | Where-Object {
        if ($_.Name -notmatch "^v$escapedBaseVersion-(\d+)_") { return $false }
        $sequence = [int]$Matches[1]
        return $sequence -ge $blockStart -and
               $sequence -lt $buildSeq -and
               -not $protected.ContainsKey($_.Name)
    }
    $archiveRoot = Join-Path $rootPath 'research\backup-archive'
    New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null
    foreach ($folder in $obsolete) {
        $destination = Join-Path $archiveRoot $folder.Name
        if (Test-Path -LiteralPath $destination) {
            $destination = Join-Path $archiveRoot ($folder.Name + '_' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        }
        Move-Item -LiteralPath $folder.FullName -Destination $destination
    }
    Write-Host "Retencao de backups: bloco $blockStart-$buildSeq consolidado; arquivados $($obsolete.Count) intermediarios em research/backup-archive/."
}

Write-Host "VPK: $output"

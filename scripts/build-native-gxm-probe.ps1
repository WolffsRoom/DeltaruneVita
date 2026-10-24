param([switch]$RefreshReference)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$baseVersion = (Get-Content -LiteralPath (Join-Path $root 'config\version.txt') -Raw).Trim()
$versionTag = "v$baseVersion-native-gxm"
$reference = Join-Path $root 'scratch\refs\nanovg-gxm'
$expectedCommit = 'ffbe2a0d4938bd5d90bba424d9f28b57bcb49cd5'

if ($RefreshReference -and (Test-Path -LiteralPath $reference)) {
    throw 'Remova manualmente scratch\refs\nanovg-gxm antes de atualizar a referência.'
}
if (-not (Test-Path -LiteralPath (Join-Path $reference 'src\nanovg_gxm.h'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reference) | Out-Null
    git clone https://github.com/xfangfang/nanovg-gxm.git $reference
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao baixar nanovg-gxm.' }
}
$actualCommit = (git -c "safe.directory=$($reference.Replace('\','/'))" -C $reference rev-parse HEAD).Trim()
if ($actualCommit -ne $expectedCommit) {
    git -c "safe.directory=$($reference.Replace('\','/'))" -C $reference checkout --detach $expectedCommit
    if ($LASTEXITCODE -ne 0) { throw "Não foi possível fixar nanovg-gxm em $expectedCommit." }
}

$dockerRoot = $root.Replace('\', '/')
docker run --rm -v "${dockerRoot}:/project" -w /project/experiments/native-gxm `
    atamanenko/vitasdk-softfp:latest sh -lc `
    "cmake -S . -B build-vita -DNANOVG_GXM_SOURCE_DIR=/project/scratch/refs/nanovg-gxm -DPORT_BUILD_VERSION=$versionTag -DCMAKE_BUILD_TYPE=Release && cmake --build build-vita -j2"
if ($LASTEXITCODE -ne 0) { throw "Build GXM falhou com código $LASTEXITCODE." }

$outputDir = Join-Path $root 'artifacts\experiments\v0.78-native-gxm'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$built = Join-Path $root "experiments\native-gxm\build-vita\Deltarune-NativeGXM-$versionTag.vpk"
$output = Join-Path $outputDir "Deltarune-NativeGXM-$versionTag.vpk"
Copy-Item -Force -LiteralPath $built -Destination $output
Write-Host "Native GXM VPK: $output"


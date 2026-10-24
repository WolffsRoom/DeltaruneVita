param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ref = Join-Path $root 'scratch\refs\nanovg-gxm\src\nanovg_gxm.h'
if (-not (Test-Path -LiteralPath $ref)) { throw 'Reference nanovg-gxm unavailable. Build regular GXM first.' }
$dockerRoot = $root.Replace('\', '/')
docker run --rm -v "${dockerRoot}:/project" -w /project/experiments/native-gxm atamanenko/vitasdk-softfp:latest sh -lc "cmake -S . -B build-audit -DNANOVG_GXM_SOURCE_DIR=/project/scratch/refs/nanovg-gxm -DGXM_DIAGNOSTIC=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-audit -j2"
if ($LASTEXITCODE -ne 0) { throw "Native GXM audit build failed with code $LASTEXITCODE." }
$outputDir = Join-Path $root 'artifacts\experiments\v0.78-native-gxm'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$built = Join-Path $root 'experiments\native-gxm\build-audit\Deltarune-GXM-Audit-v0.78-a3.vpk'
$output = Join-Path $outputDir 'Deltarune-GXM-Audit-v0.78-a3.vpk'
Copy-Item -LiteralPath $built -Destination $output -Force
Write-Host "GXM Six-Chapter Audit VPK: $output"

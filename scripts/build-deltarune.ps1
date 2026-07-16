param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'src\legacy\soloader'
$mount = $project.Replace('\', '/')

docker run --rm -v "${mount}:/project" -w /project `
    atamanenko/vitasdk-softfp:latest sh -lc `
    "cmake -S . -B build-deltarune -DCMAKE_BUILD_TYPE=$Configuration && cmake --build build-deltarune -j2"

if ($LASTEXITCODE -ne 0) {
    throw "Build falhou com código $LASTEXITCODE."
}

Write-Host "VPK: $project\build-deltarune\Deltarune.vpk"

param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$rootPath = Split-Path -Parent $PSScriptRoot
$project = Join-Path $rootPath 'src\vita-probe'
$root = $rootPath.Replace('\', '/')

docker run --rm -v "${root}:/project" -w /project/src/vita-probe `
    atamanenko/vitasdk-softfp:latest sh -lc `
    "cmake -S . -B build-vita -DCMAKE_BUILD_TYPE=$Configuration && cmake --build build-vita -j2"

if ($LASTEXITCODE -ne 0) { throw "Build falhou com codigo $LASTEXITCODE." }
$output = Join-Path $rootPath 'artifacts\current\Deltarune.vpk'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
Copy-Item -Force (Join-Path $project 'build-vita\Deltarune.vpk') $output
Write-Host "VPK: $output"

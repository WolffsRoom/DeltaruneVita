param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('Baseline','Fast')][string]$VitaGLProfile = 'Baseline'
)

$ErrorActionPreference = 'Stop'
$rootPath = Split-Path -Parent $PSScriptRoot
$project = Join-Path $rootPath 'src\vita-probe'
$root = $rootPath.Replace('\', '/')

$vitaGLFlags = 'SOFTFP_ABI=1 NO_SPLASHSCREEN=1 NO_DEBUG=1'
if ($VitaGLProfile -eq 'Fast') {
    # Keep vitaGL's disk-backed HAVE_TEXTURE_CACHE disabled: Butterscotch already
    # owns texture residency, LRU eviction, a RAM prepared-page cache and the
    # persistent .r444 cache. The fast profile only enables hot-path optimizations.
    $vitaGLFlags += ' CIRCULAR_POOL_SPEEDHACK=1 TEXTURES_SPEEDHACK=1 TEXTURE_UPLOADS_SPEEDHACK=1 SAMPLERS_SPEEDHACK=1 INDICES_SPEEDHACK=1'
}

Write-Host "vitaGL profile: $VitaGLProfile"
Write-Host "vitaGL flags:   $vitaGLFlags"

# vitaGL's Makefile does not track CFLAGS changes as object dependencies. Clean
# first so switching between Baseline/Fast actually rebuilds every object with
# the requested feature defines.
docker run --rm -v "${root}:/project" -w /project/src/vita-probe `
    atamanenko/vitasdk-softfp:latest sh -lc `
    "make -C /project/third_party/vitaGL-nosplash clean && make -C /project/third_party/vitaGL-nosplash -j2 $vitaGLFlags && cmake -S . -B build-vita -DCMAKE_BUILD_TYPE=$Configuration && cmake --build build-vita -j2"

if ($LASTEXITCODE -ne 0) { throw "Build falhou com codigo $LASTEXITCODE." }
$output = Join-Path $rootPath 'artifacts\current\Deltarune-v0.57.vpk'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
Copy-Item -Force (Join-Path $project 'build-vita\Deltarune.vpk') $output
Write-Host "VPK: $output"

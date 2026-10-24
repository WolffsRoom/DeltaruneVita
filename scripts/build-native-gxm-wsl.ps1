param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($root.Length -lt 3 -or $root[1] -ne ':') {
    throw "WSL fallback expects a Windows drive path. Got: $root"
}
$letter = $root.Substring(0,1).ToLowerInvariant()
$unixRoot = "/mnt/$letter/" + $root.Substring(3).Replace('\','/')
$wslScript = @"
set -e
export VITASDK=/usr/local/vitasdk
export PATH=/usr/local/vitasdk/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
cd '$unixRoot/experiments/native-gxm'
cmake -S . -B build-wsl-vita -G Ninja \
 -DNANOVG_GXM_SOURCE_DIR='$unixRoot/scratch/refs/nanovg-gxm' \
 -DCMAKE_BUILD_TYPE=Release
cmake --build build-wsl-vita -j4
"@
# PowerShell here-strings use CRLF on Windows. Bash -lc requires LF for
# continuation backslashes and unquoted paths.
$wslScript = $wslScript.Replace("`r", "")
wsl -d Ubuntu -- bash -lc $wslScript
if ($LASTEXITCODE -ne 0) { throw "WSL VitaSDK compilation failed ($LASTEXITCODE)." }
$source = Join-Path $root 'experiments\native-gxm\build-wsl-vita\Deltarune-NativeGXM-v0.78-gxm48.vpk'
$targetDir = Join-Path $root 'artifacts\experiments\v0.78-native-gxm'
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
$target = Join-Path $targetDir 'Deltarune-NativeGXM-v0.78-gxm48.vpk'
if (-not (Test-Path -LiteralPath $source)) { throw "Compiled VPK not found: $source" }
Copy-Item -Force -LiteralPath $source -Destination $target
Write-Host "Native GXM48 WSL-built VPK: $target"

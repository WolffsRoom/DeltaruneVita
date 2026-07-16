param(
    [string]$SourceRoot,
    [string]$ApkPath,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $SourceRoot) { $SourceRoot = Join-Path $root 'data\extracted-apk' }
if (-not $ApkPath) { $ApkPath = Join-Path $root 'data\apk\game.apk' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'data\prepared\deltarune' }

$expected = @{
    Apk = 'C25CCCCB42CD7C62FC41480B188F72367F60F123FEC395A762E072CC4DB7AC48'
    Runner = '8BA532CE664155D75B35351228C6C5809C815ADECAB367C7D4085A962C2C6670'
    Cpp = '4DF8B2E81690DD1393A94F65C18B41E5B91494B10B21415823A94AB6BB81018A'
}

$runner = Join-Path $SourceRoot 'lib\armeabi-v7a\libyoyo.so'
$cpp = Join-Path $SourceRoot 'lib\armeabi-v7a\libc++_shared.so'
$assets = Join-Path $SourceRoot 'assets'

foreach ($path in @($ApkPath, $runner, $cpp, $assets)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Arquivo obrigatório ausente: $path"
    }
}

if ((Get-FileHash -LiteralPath $ApkPath -Algorithm SHA256).Hash -ne $expected.Apk) {
    throw 'O APK não corresponde à versão suportada.'
}
if ((Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash -ne $expected.Runner) {
    throw 'libyoyo.so não corresponde à versão suportada.'
}
if ((Get-FileHash -LiteralPath $cpp -Algorithm SHA256).Hash -ne $expected.Cpp) {
    throw 'libc++_shared.so não corresponde à versão suportada.'
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
Copy-Item -LiteralPath $ApkPath -Destination (Join-Path $OutputRoot 'game.apk') -Force
Copy-Item -LiteralPath $runner -Destination (Join-Path $OutputRoot 'libyoyo.so') -Force
Copy-Item -LiteralPath $cpp -Destination (Join-Path $OutputRoot 'libc++_shared.so') -Force
Copy-Item -LiteralPath $assets -Destination $OutputRoot -Recurse -Force

Get-ChildItem -LiteralPath $OutputRoot -Recurse -File |
    Get-FileHash -Algorithm SHA256 |
    ForEach-Object { "{0}  {1}" -f $_.Hash, $_.Path.Substring($OutputRoot.Length + 1) } |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'manifest-sha256.txt') -Encoding ascii

Write-Host "Dados preparados em: $OutputRoot"
Write-Host 'Copie o conteúdo dessa pasta para ux0:data/deltarune/.'

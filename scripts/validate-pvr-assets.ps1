param(
    [string]$Root = (Join-Path (Split-Path -Parent $PSScriptRoot) 'data\pvr')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Root)) {
    throw "Pasta PVR nao encontrada: $Root"
}

$files = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.pvr')
if ($files.Count -eq 0) {
    throw 'Nenhum arquivo PVR encontrado.'
}

$valid = 0
$rgba4444Format = [UInt64]0x0404040461626772
$rgba5551Format = [UInt64]0x0105050561626772
$pvrtc1_4bppFormat = [UInt64]3
$pvrtc2_4bppFormat = [UInt64]5
$formatCounts = @{}
foreach ($file in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -lt 53) { throw "PVR incompleto: $($file.FullName)" }
    $magic = [BitConverter]::ToUInt32($bytes, 0)
    $format = [BitConverter]::ToUInt64($bytes, 8)
    $colorSpace = [BitConverter]::ToUInt32($bytes, 16)
    $height = [BitConverter]::ToUInt32($bytes, 24)
    $width = [BitConverter]::ToUInt32($bytes, 28)
    $metadata = [BitConverter]::ToUInt32($bytes, 48)
    $isPvrtc = $format -eq $pvrtc1_4bppFormat -or $format -eq $pvrtc2_4bppFormat
    if ($magic -ne 0x03525650) { throw "PVR v3 invalido: $($file.FullName)" }
    if ($format -ne $rgba4444Format -and $format -ne $rgba5551Format -and -not $isPvrtc) {
        throw "Formato hibrido nao permitido: $($file.FullName)"
    }
    if ($colorSpace -ne 1) { throw "Color space invalido (use sRGB): $($file.FullName)" }
    if ($width -eq 0 -or $height -eq 0 -or $width -gt 4096 -or $height -gt 4096) {
        throw "Dimensoes invalidas: $($file.FullName)"
    }
    if ((52 + $metadata) -ge $bytes.Length) { throw "PVR sem payload: $($file.FullName)" }
    $payloadBytes = if ($isPvrtc) {
        [int64]$width * [int64]$height / 2L
    } else {
        2L * [int64]$width * [int64]$height
    }
    $expectedLength = 52L + $metadata + $payloadBytes
    if ($bytes.Length -ne $expectedLength) { throw "Payload PVR hibrido invalido: $($file.FullName)" }
    if ($file.BaseName -notmatch '^page_[0-9]{3}$' -or
        $file.Directory.Name -notmatch '^chapter[0-5](-ptbr)?$') {
        throw "Nome/local invalido: $($file.FullName)"
    }
    $valid++
    $formatName = if ($format -eq $pvrtc1_4bppFormat) { 'PVRTC1_4BPP' } elseif ($format -eq $pvrtc2_4bppFormat) { 'PVRTC2_4BPP' } elseif ($format -eq $rgba5551Format) { 'RGBA5551' } else { 'RGBA4444' }
    $formatCounts[$formatName] = 1 + [int]$formatCounts[$formatName]
    Write-Host ("OK {0}: {1}x{2}, format={3}, sRGB, bytes={4}" -f
        $file.FullName, $width, $height, $formatName, $bytes.Length)
}

Write-Host "PVR validos: $valid"
$formatCounts.GetEnumerator() | Sort-Object Name | ForEach-Object { Write-Host ("  {0}: {1}" -f $_.Name, $_.Value) }

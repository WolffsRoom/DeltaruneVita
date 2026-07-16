param(
    [string]$Source,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'mods\PTBR' }
if (-not $Destination) { $Destination = Join-Path $root 'data\prepared\deltarune\butterscotch\mods\PTBR' }

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$launcher = Join-Path $Source 'data.win'
if (Test-Path -LiteralPath $launcher) {
    $chapter0 = Join-Path $Destination 'chapter0'
    New-Item -ItemType Directory -Force -Path $chapter0 | Out-Null
    Copy-Item -Force -LiteralPath $launcher -Destination (Join-Path $chapter0 'game.droid')
}

for ($chapter = 1; $chapter -le 5; $chapter++) {
    $sourceLang = Join-Path $Source "chapter${chapter}_windows\lang"
    $translation = Get-ChildItem -LiteralPath $sourceLang -File -Filter 'lang_pt*.json' | Select-Object -First 1
    if ($null -ne $translation) {
        $targetLang = Join-Path $Destination "chapter$chapter\lang"
        New-Item -ItemType Directory -Force -Path $targetLang | Out-Null
        Copy-Item -Force -LiteralPath $translation.FullName -Destination (Join-Path $targetLang 'lang_en.json')
        Copy-Item -Force -LiteralPath $translation.FullName -Destination (Join-Path $targetLang 'lang_ja.json')
        Copy-Item -Force -LiteralPath $translation.FullName -Destination (Join-Path $targetLang 'lang_pt.json')
        Copy-Item -Force -LiteralPath $translation.FullName -Destination (Join-Path $targetLang 'lang_ptbr.json')
        Write-Host "PT-BR capitulo $chapter -> $targetLang\lang_en.json"
        continue
    }

    $modData = Join-Path $Source "chapter${chapter}_windows\data.win"
    if (-not (Test-Path -LiteralPath $modData)) { throw "Traducao ausente para o capitulo $chapter" }
    $targetChapter = Join-Path $Destination "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $targetChapter | Out-Null
    Copy-Item -Force -LiteralPath $modData -Destination (Join-Path $targetChapter 'game.droid')
    Write-Host "PT-BR capitulo $chapter -> game.droid alternativo"
}

Write-Host "Copie a pasta 'mods' junto de ux0:data/deltarune/butterscotch/."

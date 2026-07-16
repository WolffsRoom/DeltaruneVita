param(
    [string]$Source,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'mods\PTBR' }
if (-not $Destination) { $Destination = Join-Path $root 'data\prepared\deltarune\butterscotch\mods\PTBR' }

$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $root 'data\prepared\deltarune\butterscotch\mods'))
if (-not $destinationPath.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destino fora da pasta preparada permitida: $destinationPath"
}
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Traducao ausente: $sourcePath" }

# Recria a arvore para impedir que arquivos de uma traducao anterior continuem ativos.
if (Test-Path -LiteralPath $destinationPath) {
    Remove-Item -LiteralPath $destinationPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

# O data.win da raiz pertence ao seletor de capitulos (chapter0).
$launcher = Join-Path $sourcePath 'data.win'
if (Test-Path -LiteralPath $launcher) {
    $chapter0 = Join-Path $destinationPath 'chapter0'
    New-Item -ItemType Directory -Force -Path $chapter0 | Out-Null
    Copy-Item -Force -LiteralPath $launcher -Destination (Join-Path $chapter0 'game.droid')
    Write-Host 'PT-BR chapter0 -> game.droid'
}

# Cada data.win traduzido substitui somente o runner data do capitulo. Lang, vid e
# outros complementos permanecem como overlay e usam os assets originais como fallback.
for ($chapter = 1; $chapter -le 5; $chapter++) {
    $sourceChapter = Join-Path $sourcePath "chapter$chapter"
    if (-not (Test-Path -LiteralPath $sourceChapter)) {
        Write-Host "PT-BR chapter$chapter -> nao fornecido; usara os dados originais"
        continue
    }

    $targetChapter = Join-Path $destinationPath "chapter$chapter"
    New-Item -ItemType Directory -Force -Path $targetChapter | Out-Null
    Copy-Item -Recurse -Force -Path (Join-Path $sourceChapter '*') -Destination $targetChapter

    $translatedData = Join-Path $targetChapter 'data.win'
    if (Test-Path -LiteralPath $translatedData) {
        Move-Item -Force -LiteralPath $translatedData -Destination (Join-Path $targetChapter 'game.droid')
        Write-Host "PT-BR chapter$chapter -> game.droid + overlay"
    } else {
        Write-Host "PT-BR chapter$chapter -> overlay sem game.droid"
    }
}

# Faixas modificadas ficam em uma biblioteca compartilhada do mod. O backend
# procura aqui antes de retornar para butterscotch/music.
$musicSource = Join-Path $sourcePath 'mus'
if (Test-Path -LiteralPath $musicSource) {
    $musicTarget = Join-Path $destinationPath 'music'
    New-Item -ItemType Directory -Force -Path $musicTarget | Out-Null
    Copy-Item -Force -Path (Join-Path $musicSource '*') -Destination $musicTarget
    Write-Host 'PT-BR music -> biblioteca compartilhada do mod'
}

Write-Host "Traducao preparada em: $destinationPath"
Write-Host "Copie a pasta 'mods' para ux0:data/deltarune/butterscotch/."

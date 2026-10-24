param(
    [string]$Source,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'mods\Lang\Portuguese-BR' }
if (-not $Destination) { $Destination = Join-Path $root 'mods\Lang\Portuguese-BR' }

$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $root 'mods\Lang'))
if (-not $destinationPath.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destino fora da pasta canonica mods/Lang: $destinationPath"
}
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Traducao ausente: $sourcePath" }

# The repository keeps source material grouped by language. Accept the older
# Portuguese-BR/PTBR nesting while writing only the normalized runtime tree.
if (Test-Path -LiteralPath (Join-Path $sourcePath 'PTBR') -PathType Container) {
    $sourcePath = Join-Path $sourcePath 'PTBR'
}

# mods/Lang is the canonical translation tree. When Source and Destination are
# the same directory, normalize in place and never delete/copy the source onto
# itself. An explicit different Destination is still supported for export/tests.
$sameRoot = $sourcePath.TrimEnd('\\') -ieq $destinationPath.TrimEnd('\\')
if (-not $sameRoot) {
    if (Test-Path -LiteralPath $destinationPath) {
        Remove-Item -LiteralPath $destinationPath -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

    # O data.win da raiz pertence ao seletor de capitulos (chapter0).
    $launcher = Join-Path $sourcePath 'data.win'
    if (Test-Path -LiteralPath $launcher) {
        $chapter0 = Join-Path $destinationPath 'chapter0'
        New-Item -ItemType Directory -Force -Path $chapter0 | Out-Null
        Copy-Item -Force -LiteralPath $launcher -Destination $chapter0
        Write-Host 'PT-BR chapter0 -> data.win'
    }

    for ($chapter = 1; $chapter -le 5; $chapter++) {
        $sourceChapter = Join-Path $sourcePath "chapter$chapter"
        if (-not (Test-Path -LiteralPath $sourceChapter)) {
            Write-Host "PT-BR chapter$chapter -> nao fornecido; usara os dados originais"
            continue
        }
        $targetChapter = Join-Path $destinationPath "chapter$chapter"
        New-Item -ItemType Directory -Force -Path $targetChapter | Out-Null
        Copy-Item -Recurse -Force -Path (Join-Path $sourceChapter '*') -Destination $targetChapter
    }
} else {
    Write-Host "PT-BR canonical in-place: $destinationPath"
}

# A tradução possui seu próprio data.win e precisa receber o mesmo recorte de
# mosaicos usado pelo Chapter 2 oficial; copiar apenas o patch inglês não é
# seguro porque os atlas e o bytecode traduzidos têm layouts independentes.
$umtCli = Join-Path $root 'tools\UndertaleModToolCLI\UndertaleModCli.exe'
$rollercoasterPatch = Join-Path $root 'tools\PatchChapter2Rollercoaster.csx'
$chapter2Data = Join-Path $destinationPath 'chapter2\data.win'
if ((Test-Path -LiteralPath $umtCli) -and (Test-Path -LiteralPath $rollercoasterPatch) -and
    (Test-Path -LiteralPath $chapter2Data)) {
    $patchedData = Join-Path $destinationPath 'chapter2\data.rollercoaster.win'
    & $umtCli load $chapter2Data -s $rollercoasterPatch -o $patchedData
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao otimizar o rollercoaster do Chapter 2 PT-BR.' }
    Move-Item -Force -LiteralPath $patchedData -Destination $chapter2Data
}

# Faixas modificadas ficam em uma biblioteca compartilhada do mod. O backend
# procura aqui antes de retornar para deltarunevita/music.
$musicSource = Join-Path $sourcePath 'mus'
if (Test-Path -LiteralPath $musicSource) {
    $musicTarget = Join-Path $destinationPath 'music'
    New-Item -ItemType Directory -Force -Path $musicTarget | Out-Null
    Copy-Item -Force -Path (Join-Path $musicSource '*') -Destination $musicTarget
    Write-Host 'PT-BR music -> biblioteca compartilhada do mod'
}

# Language-specific compressed atlases never enter the game's global pvr
# directory. At runtime they are resolved from mods/Lang/<language>/pvr.
# Normalized language packages keep their private atlases inside the language
# directory. Retain the sibling lookup only for the older PTBR nesting.
$pvrSource = Join-Path $sourcePath 'pvr'
if (-not (Test-Path -LiteralPath $pvrSource -PathType Container)) {
    $pvrSource = Join-Path ([IO.Path]::GetDirectoryName($sourcePath)) 'pvr'
}
if ((-not $sameRoot) -and (Test-Path -LiteralPath $pvrSource -PathType Container)) {
    $pvrTarget = Join-Path $destinationPath 'pvr'
    New-Item -ItemType Directory -Force -Path $pvrTarget | Out-Null
    Get-ChildItem -LiteralPath $pvrSource -Directory | ForEach-Object {
        $chapterName = $_.Name -replace '-ptbr$', ''
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $pvrTarget $chapterName) -Recurse -Force
    }
    Write-Host 'PT-BR PVR -> perfil privado da traducao'
}

Write-Host "Traducao canonica em: $destinationPath"
Write-Host "Copie a pasta 'mods' para ux0:data/deltarune/deltarunevita/."

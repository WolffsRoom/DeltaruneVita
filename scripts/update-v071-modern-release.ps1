$ErrorActionPreference = 'Stop'

$repo = 'WolffsRoom/DeltaruneVita'
$tag = 'v0.71'
$vpk = Join-Path $PSScriptRoot '..\artifacts\current\Deltarune-v0.71-22.vpk'

$release = gh release view $tag --repo $repo --json body,assets | ConvertFrom-Json
$body = [string]$release.body

$modernNotes = @'
### Modern GL compatibility update

- **Files:** `src/butterscotch/src/gl/gl_renderer.c`, `src/vita-probe/source/playable_main.c`
- **Before:** Modern GL compiled every chapter shader during startup, used persistent VAO state on VitaGL and searched for RGBA4444 caches in the obsolete global layout. This could stop at the loading screen, produce a black frame or hard-lock the GPU before the first Room.
- **After:** GameMaker GLSL ES shaders are compiled on demand, the Vita backend uses the safer explicit VBO attribute path, and startup performs a programmable-pipeline preflight with automatic Legacy GL fallback.
- Modern GL now reads the chapter-local cache layout, including language variants and the Chapter 3 VTC5/VTC6 cache markers.
- Fixed an out-of-bounds surface lookup that iterated over Texture Page count instead of the allocated surface count.
- Modern GL remains **experimental** and requires validation on Vita hardware, especially in Chapters 3 and 5.

'@

if ($body -notmatch '### Modern GL compatibility update') {
    $anchor = '### Others Improvements Carried Forward'
    if ($body.Contains($anchor)) {
        $body = $body.Replace($anchor, $modernNotes + $anchor)
    } else {
        $body += "`r`n`r`n" + $modernNotes
    }
}

$body = $body.Replace(
    '- **Deltarune-v0.71-1.vpk** - Initial v0.71 (Internal Test Build)',
    '- **Deltarune-v0.71-22.vpk** - Current v0.71 internal test build with guarded Modern GL startup and chapter-local texture-cache support'
)

foreach ($asset in $release.assets) {
    if ($asset.name -like '*.vpk' -and $asset.name -ne 'Deltarune-v0.71-22.vpk') {
        gh release delete-asset $tag $asset.name --repo $repo --yes
        if ($LASTEXITCODE -ne 0) { throw "Failed to remove release asset: $($asset.name)" }
    }
}

if (-not ($release.assets.name -contains 'Deltarune-v0.71-22.vpk')) {
    gh release upload $tag $vpk --repo $repo
    if ($LASTEXITCODE -ne 0) { throw 'Failed to upload v0.71-22 VPK' }
}

$body | gh release edit $tag --repo $repo --notes-file - --prerelease
if ($LASTEXITCODE -ne 0) { throw 'Failed to update v0.71 release notes' }

Write-Output 'v0.71 updated successfully.'

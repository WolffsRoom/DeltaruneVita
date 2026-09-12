# DeltaruneVita Web Patcher

This branch contains only the files required by the browser-based Seam's Patcher.

## Layout

- `index.html` — Web Patcher UI.
- `assets/` — UI/audio/static assets.
- `embedded/` — embedded patch resources.
- `patch_data/` — browser patch data.
- `manifest.json` / `manifest.js` — base patch manifests.
- `language_packs.json` — canonical language-pack catalog.
- `language-packs/language_packs.json` — compatibility mirror used by existing patchers.
- `language-packs/v0.73/` — chunked v0.73 language packages. These files are stored in the branch for raw GitHub delivery and excluded from the published Pages artifact.

Large language packages are split into parts below GitHub's normal per-file limit. The patchers verify every part and the reassembled `.langpack` with SHA-256 before applying it.

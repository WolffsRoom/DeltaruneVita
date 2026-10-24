from __future__ import annotations

import argparse
import bz2
import gzip
import hashlib
import json
import shutil
import struct
import tempfile
import zipfile
from pathlib import Path

import bsdiff4

ROOT = Path(__file__).resolve().parents[1]
LANG_ROOT = ROOT / "mods" / "Lang"
STEAM_ROOT = ROOT / "artifacts" / "Patcher" / "Build_Patch" / "SteamFiles" / "DELTARUNE"
OUT_ROOT = ROOT / "artifacts" / "LanguagePacks"
SAFE_EXTRA_EXT = {".json", ".txt", ".md", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".ini", ".cfg", ".xml", ".ogg", ".wav", ".mp4"}
SKIP_NAMES = {"prepared_manifest.json", "texture_manifest.json", "data.win.vita-patch.txt", "first_load.mp4"}
SKIP_PARTS = {"pvr", "texture-cache"}
DISPLAY = {
    "Portuguese-BR": ("Português (BR)", ["ptbr", "pt"]),
    "Spanish": ("Español", ["es"]),
    "French": ("Français", ["fr"]),
    "German": ("Deutsch", ["de"]),
    "Russkiy": ("РУССКИЙ", ["ru"]),
    "Turkish": ("Türkçe", ["tr"]),
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _bsdiff_int64(data: bytes) -> int:
    value = data[7] & 0x7F
    for index in range(6, -1, -1):
        value = value * 256 + data[index]
    return -value if data[7] & 0x80 else value


def browser_patch_from_bsdiff(path: Path) -> bytes:
    patch = path.read_bytes()
    if len(patch) < 32 or patch[:8] != b"BSDIFF40":
        raise RuntimeError(f"Invalid BSDIFF patch: {path}")
    ctrl_len = _bsdiff_int64(patch[8:16])
    diff_len = _bsdiff_int64(patch[16:24])
    new_size = _bsdiff_int64(patch[24:32])
    ctrl = bz2.decompress(patch[32:32 + ctrl_len])
    diff = bz2.decompress(patch[32 + ctrl_len:32 + ctrl_len + diff_len])
    extra = bz2.decompress(patch[32 + ctrl_len + diff_len:])
    if len(ctrl) % 24:
        raise RuntimeError(f"Invalid BSDIFF control block: {path}")
    triples = [tuple(_bsdiff_int64(ctrl[o + i:o + i + 8]) for i in (0, 8, 16)) for o in range(0, len(ctrl), 24)]
    raw = bytearray(b"BSP1")
    raw += struct.pack("<IIII", new_size, len(triples), len(diff), len(extra))
    for x, y, z in triples:
        raw += struct.pack("<iii", x, y, z)
    raw += diff + extra
    return gzip.compress(bytes(raw), compresslevel=9)


def steam_source(rel: Path) -> Path | None:
    parts = rel.parts
    if not parts:
        return None
    first = parts[0].lower()
    if first == "chapter0":
        return STEAM_ROOT.joinpath(*parts[1:])
    if first.startswith("chapter") and first[7:].isdigit():
        return STEAM_ROOT / f"{first}_windows" / Path(*parts[1:])
    return None


def build_language(lang: str, version: str) -> dict:
    src_root = LANG_ROOT / lang
    if not src_root.is_dir():
        raise SystemExit(f"Language not found: {src_root}")
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    final = OUT_ROOT / f"{lang}-v{version}.langpack"
    records: list[dict] = []
    skipped: list[str] = []
    with tempfile.TemporaryDirectory() as td:
        stage = Path(td)
        (stage / "patches").mkdir()
        (stage / "browser_patches").mkdir()
        (stage / "extras").mkdir()
        patch_no = 0
        files = sorted(p for p in src_root.rglob("*") if p.is_file())
        for i, p in enumerate(files, 1):
            rel = p.relative_to(src_root)
            lower_parts = {x.lower() for x in rel.parts}
            if p.name.lower() in SKIP_NAMES or lower_parts & SKIP_PARTS:
                continue
            source = steam_source(rel)
            if source and source.is_file():
                src_hash = sha256(source)
                out_hash = sha256(p)
                rec = {
                    "output": rel.as_posix(),
                    "source": source.relative_to(STEAM_ROOT).as_posix(),
                    "source_sha256": src_hash,
                    "source_size": source.stat().st_size,
                    "output_sha256": out_hash,
                    "size": p.stat().st_size,
                }
                if src_hash == out_hash:
                    rec["mode"] = "copy"
                else:
                    patch_name = f"{patch_no:04d}.bsdiff"
                    browser_name = f"{patch_no:04d}.bsp.gz"
                    patch_no += 1
                    patch_path = stage / "patches" / patch_name
                    bsdiff4.file_diff(str(source), str(p), str(patch_path))
                    (stage / "browser_patches" / browser_name).write_bytes(browser_patch_from_bsdiff(patch_path))
                    rec.update(mode="patch", patch=f"patches/{patch_name}", browser_patch=f"browser_patches/{browser_name}")
                records.append(rec)
            elif p.suffix.lower() in SAFE_EXTRA_EXT:
                dst = stage / "extras" / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, dst)
                records.append({
                    "output": rel.as_posix(),
                    "mode": "extra",
                    "extra": f"extras/{rel.as_posix()}",
                    "output_sha256": sha256(p),
                    "size": p.stat().st_size,
                })
            else:
                skipped.append(rel.as_posix())
            if i % 100 == 0:
                print(f"{lang}: scanned {i}/{len(files)}")
        display_name, interface_codes = DISPLAY.get(lang, (lang, []))
        manifest = {
            "format": 2,
            "language": lang,
            "display_name": display_name,
            "interface_codes": interface_codes,
            "port_version": version,
            "output_root": f"deltarunevita/mods/Lang/{lang}",
            "source_game": "DELTARUNE Steam",
            "contains_game_files": False,
            "texture_assets": "runtime-generated",
            "texture_policy": {
                "ship_pvr": False,
                "ship_texture_cache": False,
                "bc3": "generated by DeltaruneVita runtime when policy selects BC3",
                "rgba4444": "generated/cached by DeltaruneVita runtime for lossless pages",
            },
            "records": records,
            "skipped_unclassified": skipped,
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        with zipfile.ZipFile(final, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for f in sorted(stage.rglob("*")):
                if f.is_file():
                    z.write(f, f.relative_to(stage).as_posix())
    print(f"Created {final} ({final.stat().st_size / 1024 / 1024:.1f} MiB), records={len(records)}, skipped={len(skipped)}")
    return {
        "id": lang,
        "display_name": display_name,
        "interface_codes": interface_codes,
        "file": final.name,
        "sha256": sha256(final),
        "size": final.stat().st_size,
        "records": len(records),
        "ready": True,
        "skipped": skipped,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("languages", nargs="*")
    ap.add_argument("--version", default="0.73")
    args = ap.parse_args()
    langs = args.languages or [p.name for p in LANG_ROOT.iterdir() if p.is_dir() and p.name != "icons" and "Pendente" not in p.name]
    results = [build_language(x, args.version) for x in langs]
    index = {"format": 2, "port_version": args.version, "repository": "WolffsRoom/DeltaruneVita@gh-pages", "packs": results}
    (OUT_ROOT / "build-index.json").write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

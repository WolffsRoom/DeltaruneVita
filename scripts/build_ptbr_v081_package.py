from __future__ import annotations

import bz2
import gzip
import hashlib
import json
from pathlib import Path
import struct
import zipfile

import bsdiff4

ROOT = Path(__file__).resolve().parents[1]
STEAM_ROOT = ROOT / "artifacts" / "Patcher" / "Build_Patch" / "SteamFiles" / "DELTARUNE"
LANG_ROOT = ROOT / "mods" / "Lang" / "Portuguese-BR"
OLD_PACK = ROOT / "artifacts" / "LanguagePacks" / "Portuguese-BR-v0.73.langpack"
OUT_PACK = ROOT / "artifacts" / "LanguagePacks" / "Portuguese-BR-v0.81.langpack"
WEBPATCHER_DIR = ROOT / "artifacts" / "Patcher" / "Build_Patch" / "webpatcher"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _bsdiff_int64(data: bytes) -> int:
    value = data[7] & 0x7F
    for index in range(6, -1, -1):
        value = value * 256 + data[index]
    return -value if data[7] & 0x80 else value


def browser_patch_from_bsdiff(patch: bytes) -> bytes:
    if len(patch) < 32 or patch[:8] != b"BSDIFF40":
        raise RuntimeError("Invalid BSDIFF patch")
    ctrl_len = _bsdiff_int64(patch[8:16])
    diff_len = _bsdiff_int64(patch[16:24])
    new_size = _bsdiff_int64(patch[24:32])
    ctrl = bz2.decompress(patch[32:32 + ctrl_len])
    diff = bz2.decompress(patch[32 + ctrl_len:32 + ctrl_len + diff_len])
    extra = bz2.decompress(patch[32 + ctrl_len + diff_len:])
    if len(ctrl) % 24:
        raise RuntimeError("Invalid BSDIFF control block")
    triples = [tuple(_bsdiff_int64(ctrl[o + i:o + i + 8]) for i in (0, 8, 16)) for o in range(0, len(ctrl), 24)]
    raw = bytearray(b"BSP1")
    raw += struct.pack("<IIII", new_size, len(triples), len(diff), len(extra))
    for x, y, z in triples:
        raw += struct.pack("<iii", x, y, z)
    raw += diff + extra
    return gzip.compress(bytes(raw), compresslevel=9)


def main():
    print("Reading old v0.73 pack...")
    with zipfile.ZipFile(OLD_PACK, "r") as old_zip:
        old_manifest = json.loads(old_zip.read("manifest.json").decode("utf-8"))
        old_records = old_manifest["records"]

        # Stage files in memory or zip
        members = {info.filename: old_zip.read(info.filename) for info in old_zip.infolist() if info.filename != "manifest.json"}

    print("Diffing Chapter 5 files...")
    # 1. chapter5/vid/ch5_intro_en.mp4
    steam_vid = STEAM_ROOT / "chapter5_windows" / "vid" / "ch5_intro_en.mp4"
    mod_vid = LANG_ROOT / "chapter5" / "vid" / "ch5_intro_en.mp4"
    print("  Creating bsdiff for video...")
    vid_patch = bsdiff4.diff(steam_vid.read_bytes(), mod_vid.read_bytes())
    vid_bsp = browser_patch_from_bsdiff(vid_patch)
    members["patches/0008.bsdiff"] = vid_patch
    members["browser_patches/0008.bsp.gz"] = vid_bsp

    # 2. chapter5/data.win
    steam_win = STEAM_ROOT / "chapter5_windows" / "data.win"
    mod_win = LANG_ROOT / "chapter5" / "data.win"
    print("  Creating bsdiff for data.win (165MB -> 304MB)...")
    win_patch = bsdiff4.diff(steam_win.read_bytes(), mod_win.read_bytes())
    win_bsp = browser_patch_from_bsdiff(win_patch)
    members["patches/0009.bsdiff"] = win_patch
    members["browser_patches/0009.bsp.gz"] = win_bsp

    # 3. chapter5/lang/lang_en.json
    mod_lang = LANG_ROOT / "chapter5" / "lang" / "lang_en.json"
    members["extras/chapter5/lang/lang_en.json"] = mod_lang.read_bytes()

    # Build new records list
    new_records = list(old_records)
    new_records.append({
        "output": "chapter5/vid/ch5_intro_en.mp4",
        "source": "chapter5_windows/vid/ch5_intro_en.mp4",
        "source_sha256": sha256_file(steam_vid),
        "source_size": steam_vid.stat().st_size,
        "output_sha256": sha256_file(mod_vid),
        "size": mod_vid.stat().st_size,
        "mode": "patch",
        "patch": "patches/0008.bsdiff",
        "browser_patch": "browser_patches/0008.bsp.gz"
    })
    new_records.append({
        "output": "chapter5/data.win",
        "source": "chapter5_windows/data.win",
        "source_sha256": sha256_file(steam_win),
        "source_size": steam_win.stat().st_size,
        "output_sha256": sha256_file(mod_win),
        "size": mod_win.stat().st_size,
        "mode": "patch",
        "patch": "patches/0009.bsdiff",
        "browser_patch": "browser_patches/0009.bsp.gz"
    })
    new_records.append({
        "output": "chapter5/lang/lang_en.json",
        "mode": "extra",
        "extra": "extras/chapter5/lang/lang_en.json",
        "output_sha256": sha256_file(mod_lang),
        "size": mod_lang.stat().st_size
    })

    new_manifest = dict(old_manifest)
    new_manifest["port_version"] = "0.81"
    new_manifest["records"] = new_records
    manifest_bytes = (json.dumps(new_manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    members["manifest.json"] = manifest_bytes

    print(f"Writing {OUT_PACK}...")
    with zipfile.ZipFile(OUT_PACK, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as out_zip:
        for fname in sorted(members.keys()):
            out_zip.writestr(fname, members[fname])

    pack_size = OUT_PACK.stat().st_size
    pack_hash = sha256_file(OUT_PACK)
    print(f"Packed {OUT_PACK} successfully! Size: {pack_size} bytes ({pack_size/1024/1024:.1f} MB), SHA-256: {pack_hash}")

    # Split into 75 MB chunks
    chunk_size = 78643200
    parts_meta = []
    chunk_dir = WEBPATCHER_DIR / "language-packs" / "v0.81" / "Portuguese-BR"
    chunk_dir.mkdir(parents=True, exist_ok=True)

    with open(OUT_PACK, "rb") as pf:
        chunk_idx = 0
        while True:
            chunk = pf.read(chunk_size)
            if not chunk:
                break
            part_name = f"part_{chunk_idx:02d}.bin"
            part_path = chunk_dir / part_name
            part_path.write_bytes(chunk)
            part_hash = sha256_bytes(chunk)
            parts_meta.append({
                "url": f"https://raw.githubusercontent.com/WolffsRoom/DeltaruneVita/gh-pages/language-packs/v0.81/Portuguese-BR/{part_name}",
                "size": len(chunk),
                "sha256": part_hash
            })
            print(f"  Chunk {part_name}: {len(chunk)} bytes, SHA-256: {part_hash}")
            chunk_idx += 1

    # Also mirror chunks in local relative directory for local web patcher testing
    local_chunk_dir = WEBPATCHER_DIR / "language-packs" / "Portuguese-BR-v0.81"
    local_chunk_dir.mkdir(parents=True, exist_ok=True)
    for p in chunk_dir.glob("*.bin"):
        (local_chunk_dir / p.name).write_bytes(p.read_bytes())

    # Update webpatcher/language_packs.json
    lp_json_path = WEBPATCHER_DIR / "language_packs.json"
    with open(lp_json_path, "r", encoding="utf-8") as f:
        lp_data = json.load(f)

    lp_data["port_version"] = "0.81"
    for lang in lp_data.get("languages", []):
        if lang.get("id") == "Portuguese-BR":
            lang["file"] = "Portuguese-BR-v0.81.langpack"
            lang["sha256"] = pack_hash
            lang["size"] = pack_size
            lang["parts"] = parts_meta
            print("Updated Portuguese-BR in language_packs.json")

    with open(lp_json_path, "w", encoding="utf-8") as f:
        json.dump(lp_data, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # Mirror language_packs.json into webpatcher/language-packs/language_packs.json
    (WEBPATCHER_DIR / "language-packs" / "language_packs.json").write_text(
        json.dumps(lp_data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print("Done updating language_packs.json!")


if __name__ == "__main__":
    main()


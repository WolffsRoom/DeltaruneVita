"""Create the DELTARUNE Vita redistributable patch bundle from Steam files."""
from __future__ import annotations

import hashlib
import bz2
import gzip
import json
import shutil
import struct
import tempfile
from pathlib import Path

import bsdiff4
from PIL import Image


BUILD_PATCH_ROOT = Path(__file__).resolve().parent.parent
PROJECT_ROOT = Path(__file__).resolve().parents[4]
CENTRAL_LANG_ROOT = PROJECT_ROOT / "mods" / "Lang"
STEAM_ROOT = BUILD_PATCH_ROOT / "SteamFiles" / "DELTARUNE"
DEV_ROOT = BUILD_PATCH_ROOT / "VitaFiles" / "deltarune"
PATCH_ROOT = BUILD_PATCH_ROOT / "patch_data"
PORT_VERSION = "0.73"


def prepare_vita_output(source: Path, relative: Path, temporary: Path) -> Path:
    """Apply PC-side conversions that should not be repeated on the Vita."""
    parts = tuple(part.lower() for part in relative.parts)
    if "borders" not in parts or source.suffix.lower() != ".png":
        return source
    with Image.open(source) as image:
        if image.size[0] <= 960 and image.size[1] <= 544:
            return source
        target = temporary / "optimized" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        image.convert("RGBA").resize((960, 544), Image.Resampling.LANCZOS).save(
            target, format="PNG", optimize=True, compress_level=9
        )
        return target


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _bsdiff_int64(data: bytes) -> int:
    """Decode BSDIFF40's sign-magnitude little-endian 64-bit integer."""
    value = data[7] & 0x7F
    for index in range(6, -1, -1):
        value = value * 256 + data[index]
    return -value if data[7] & 0x80 else value


def browser_patch_from_bsdiff(path: Path) -> bytes:
    """Convert BSDIFF40+bzip2 to a gzip-compressed browser patch.

    Browsers expose gzip through DecompressionStream but not bzip2. The body
    retains BSDIFF's compact control/diff/extra model and avoids redistributing
    a complete modified data.win.
    """
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
    triples: list[tuple[int, int, int]] = []
    for offset in range(0, len(ctrl), 24):
        triples.append(tuple(_bsdiff_int64(ctrl[offset + i:offset + i + 8]) for i in (0, 8, 16)))
    raw = bytearray(b"BSP1")
    raw += struct.pack("<IIII", new_size, len(triples), len(diff), len(extra))
    for x, y, z in triples:
        raw += struct.pack("<iii", x, y, z)
    raw += diff + extra
    return gzip.compress(bytes(raw), compresslevel=9)


def expected_source(relative: Path) -> Path | None:
    parts = relative.parts
    if not parts or parts[0].lower() != "deltarunevita":
        return None
    inner = Path(*parts[1:])
    if inner.parts and inner.parts[0].lower() == "music":
        return Path("mus", *inner.parts[1:])
    if inner.parts and inner.parts[0].lower().startswith("chapter"):
        chapter = inner.parts[0].lower()
        tail = Path(*inner.parts[1:])
        if chapter == "chapter0":
            return tail
        number = chapter.removeprefix("chapter")
        return Path(f"chapter{number}_windows", tail)
    return inner


def redundant_vita_copy(path: Path) -> str | None:
    """Identify output copies that the Vita runtime already resolves elsewhere."""
    relative = path.relative_to(DEV_ROOT)
    parts = tuple(part.lower() for part in relative.parts)

    # options.ini/dr.ini are local GameMaker/PC preferences, not owned game
    # data required by the Vita runner. Steam may create or omit them depending
    # on whether each chapter has been launched, so never require, patch or
    # redistribute INI files.
    if path.suffix.lower() == ".ini" and not (len(parts) >= 3 and parts[:3] == ("deltarunevita", "ui", "lang")):
        return "local INI configuration"

    # Native desktop libraries and executables can be present beside a
    # chapter's data.win, but the Vita runner cannot load Windows binaries.
    # Requiring them also rejects valid Steam layouts where optional helper
    # DLLs were not installed. Never copy or require them in a Vita package.
    if path.suffix.lower() in {".dll", ".exe", ".pdb", ".so", ".dylib"}:
        return "desktop binary"

    # data/prepared/deltarune/Revisado is a local comparison tree, not part of
    # ux0:data/deltarune. Refuse it here as an additional safety net if someone
    # points DEV_ROOT at the prepared directory without first synchronizing
    # only its canonical deltarunevita subtree.
    if parts and parts[0] == "revisado":
        return "local reviewed comparison tree"

    # Translation sources are centralized at DeltaruneVita/mods/Lang.
    # Build_Patch/VitaFiles must never become a second source of truth.
    if len(parts) >= 3 and parts[:3] == ("deltarunevita", "mods", "lang"):
        return "translation source belongs to DeltaruneVita/mods/Lang"
    if len(parts) >= 3 and parts[:2] == ("deltarunevita", "pvr") and parts[2].endswith("-ptbr"):
        return "optional PTBR PVR download"

    # The runner rebuilds this fallback from the user's own data.win whenever
    # PVR is disabled. Shipping it together with the ready BC3 set duplicated
    # roughly 1.36 GiB in every desktop/web package.
    if "pvr" in parts:
        return "regenerable PVR/BC3 texture cache"

    if "texture-cache" in parts:
        return "regenerable RGBA4444 texture cache"

    # This report describes the files produced on the user's machine and is
    # recreated by the patcher after validation. Bundling an old report would
    # temporarily expose stale version and hash information.
    if parts == ("deltarunevita", "patcher-report.json"):
        return "runtime-generated patcher report"

    # Older translation layouts kept game.droid beside the canonical data.win.
    # The Vita launcher only opens data.win, so keep game.droid only when it is
    # not an exact duplicate (which makes an unexpected source layout visible).
    if path.name.lower() == "game.droid":
        canonical = path.with_name("data.win")
        if canonical.is_file() and path.stat().st_size == canonical.stat().st_size:
            if sha256_file(path) == sha256_file(canonical):
                return "duplicate mod game.droid"

    # The audio backend deliberately falls back from chapter0/mus/<track> to
    # deltarunevita/music/<track>. Steam v0.0.253 contains the same 339 files in
    # both locations in the prepared tree, so installing both wastes ~285 MiB.
    if len(parts) >= 4 and parts[:3] == ("deltarunevita", "chapter0", "mus"):
        shared = DEV_ROOT / "deltarunevita" / "music" / path.name
        if shared.is_file() and path.stat().st_size == shared.stat().st_size:
            if sha256_file(path) == sha256_file(shared):
                return "duplicate chapter0 shared music"

    return None


def main() -> None:
    if not (STEAM_ROOT / "DELTARUNE.exe").is_file() or not (STEAM_ROOT / "data.win").is_file():
        raise SystemExit(f"Steam installation not found in: {STEAM_ROOT}")
    if not DEV_ROOT.is_dir():
        raise SystemExit(f"Prepared Vita files not found in: {DEV_ROOT}")
    if not CENTRAL_LANG_ROOT.is_dir():
        raise SystemExit(f"Central language source not found in: {CENTRAL_LANG_ROOT}")
    print(f"Central language source: {CENTRAL_LANG_ROOT}")

    steam_files = sorted(p for p in STEAM_ROOT.rglob("*") if p.is_file())
    print(f"Indexing {len(steam_files)} Steam files...")
    hashes: dict[str, list[Path]] = {}
    for path in steam_files:
        hashes.setdefault(sha256_file(path), []).append(path)

    if PATCH_ROOT.exists():
        try:
            shutil.rmtree(PATCH_ROOT, ignore_errors=True)
        except Exception:
            pass
    (PATCH_ROOT / "patches").mkdir(parents=True, exist_ok=True)
    (PATCH_ROOT / "embedded").mkdir(parents=True, exist_ok=True)

    records: list[dict[str, object]] = []
    required: dict[str, dict[str, object]] = {}
    all_vita_files = sorted(p for p in DEV_ROOT.rglob("*") if p.is_file())
    vita_files: list[Path] = []
    omitted: dict[str, int] = {}
    omitted_bytes = 0
    for path in all_vita_files:
        reason = redundant_vita_copy(path)
        if reason is None:
            vita_files.append(path)
            continue
        omitted[reason] = omitted.get(reason, 0) + 1
        omitted_bytes += path.stat().st_size
    for reason, count in sorted(omitted.items()):
        print(f"Omitting {count} safe redundant files: {reason}")
    if omitted_bytes:
        print(f"Saved {omitted_bytes / 1024 / 1024:.1f} MiB from the Vita output.")
    with tempfile.TemporaryDirectory() as td:
        temp = Path(td)
        for number, output in enumerate(vita_files):
            relative = output.relative_to(DEV_ROOT)
            prepared_output = prepare_vita_output(output, relative, temp)
            output_hash = sha256_file(prepared_output)
            source: Path | None = None

            identical = hashes.get(output_hash)
            if identical:
                preferred = expected_source(relative)
                preferred_abs = STEAM_ROOT / preferred if preferred else None
                source = preferred_abs if preferred_abs in identical else identical[0]
                mode = "copy"
            else:
                candidate = expected_source(relative)
                candidate_abs = STEAM_ROOT / candidate if candidate else None
                source = candidate_abs if candidate_abs and candidate_abs.is_file() else None
                # "patch" needs a Steam source to diff against; without one (Vita-exclusive
                # assets like ui/sounds/borders) there is nothing to diff, so the file is
                # bundled as-is ("embed") instead of wrapping it in a no-op bsdiff patch.
                mode = "patch" if source else "embed"

            source_rel = source.relative_to(STEAM_ROOT).as_posix() if source else None
            record: dict[str, object] = {
                "output": relative.as_posix(), "source": source_rel,
                "size": prepared_output.stat().st_size, "sha256": output_hash, "mode": mode,
            }
            if source:
                required[source_rel] = {"size": source.stat().st_size, "sha256": sha256_file(source)}
            if mode == "patch":
                patch_name = f"{number:04d}.bsdiff"
                old_path = temp / "old"
                shutil.copyfile(source, old_path)
                bsdiff4.file_diff(str(old_path), str(prepared_output), str(PATCH_ROOT / "patches" / patch_name))
                record["patch"] = patch_name
            elif mode == "embed":
                embed_path = PATCH_ROOT / "embedded" / relative
                embed_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(prepared_output, embed_path)
            records.append(record)
            if (number + 1) % 25 == 0 or number + 1 == len(vita_files):
                print(f"  processed {number + 1}/{len(vita_files)}")

    manifest = {
        "format": 4, "game": "DELTARUNE", "port_version": PORT_VERSION,
        "output_folder": "deltarune", "steam_folder": "DELTARUNE",
        "optimizations": {
            "console_borders": "RGBA PNG 960x544",
            "shared_chapter0_music": "identical chapter0/mus copies omitted",
            "canonical_mod_data": "identical game.droid copies omitted",
            "language_source": str(CENTRAL_LANG_ROOT),
            "optional_translations": "not embedded in the base patch",
            "omitted_files": sum(omitted.values()),
            "omitted_bytes": omitted_bytes,
        },
        "required_sources": required, "files": records,
    }
    manifest_text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    manifest_js_text = "const manifestData = " + json.dumps(manifest, indent=2, ensure_ascii=False) + ";\n"

    (PATCH_ROOT / "manifest.json").write_text(manifest_text, encoding="utf-8")
    (PATCH_ROOT / "manifest.js").write_text(manifest_js_text, encoding="utf-8")

    WEBPATCHER_ROOT = BUILD_PATCH_ROOT / "webpatcher"
    if WEBPATCHER_ROOT.exists():
        # Convert BSDIFF's bzip2 streams to gzip, which browsers can decompress
        # natively. This preserves compact binary differences for every
        # modified data.win without embedding proprietary game data.
        web_manifest = json.loads(manifest_text)
        web_patch_root = WEBPATCHER_ROOT / "patch_data" / "patches"
        if web_patch_root.parent.exists():
            shutil.rmtree(web_patch_root.parent)
        web_patch_root.mkdir(parents=True, exist_ok=True)
        for record in web_manifest["files"]:
            if record["mode"] != "patch":
                continue
            browser_name = Path(record["patch"]).with_suffix(".bsp.gz").name
            payload = browser_patch_from_bsdiff(PATCH_ROOT / "patches" / record["patch"])
            (web_patch_root / browser_name).write_bytes(payload)
            record["mode"] = "browser_patch"
            record["patch"] = browser_name
        web_manifest_text = json.dumps(web_manifest, indent=2, ensure_ascii=False) + "\n"
        web_manifest_js = "const manifestData = " + json.dumps(web_manifest, indent=2, ensure_ascii=False) + ";\n"
        (WEBPATCHER_ROOT / "manifest.json").write_text(web_manifest_text, encoding="utf-8")
        (WEBPATCHER_ROOT / "manifest.js").write_text(web_manifest_js, encoding="utf-8")
        web_embedded = WEBPATCHER_ROOT / "embedded"
        if web_embedded.exists():
            shutil.rmtree(web_embedded)
        if (PATCH_ROOT / "embedded").exists():
            shutil.copytree(PATCH_ROOT / "embedded", web_embedded)

    patch_size = sum(p.stat().st_size for p in (PATCH_ROOT / "patches").glob("*"))
    print(f"Created {len(records)} records and {patch_size / 1024 / 1024:.1f} MiB of binary patches.")


if __name__ == "__main__":
    main()



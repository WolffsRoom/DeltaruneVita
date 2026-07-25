"""Create the DELTARUNE Vita redistributable patch bundle from Steam files."""
from __future__ import annotations

import hashlib
import json
import shutil
import tempfile
from pathlib import Path

import bsdiff4
from PIL import Image


ROOT = Path(__file__).resolve().parents[4]
STEAM_ROOT = ROOT / "SteamFiles" / "v0.0.250" / "DELTARUNE"
DEV_ROOT = ROOT / "data" / "prepared" / "deltarune" / "deltarunevita"
PATCH_ROOT = ROOT / "artifacts" / "Patcher" / "patch_data"
PORT_VERSION = "0.63"


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


def main() -> None:
    if not (STEAM_ROOT / "DELTARUNE.exe").is_file() or not (STEAM_ROOT / "data.win").is_file():
        raise SystemExit(f"Steam installation not found in: {STEAM_ROOT}")
    if not DEV_ROOT.is_dir():
        raise SystemExit(f"Prepared Vita files not found in: {DEV_ROOT}")

    steam_files = sorted(p for p in STEAM_ROOT.rglob("*") if p.is_file())
    print(f"Indexing {len(steam_files)} Steam files...")
    hashes: dict[str, list[Path]] = {}
    for path in steam_files:
        hashes.setdefault(sha256_file(path), []).append(path)

    if PATCH_ROOT.exists():
        shutil.rmtree(PATCH_ROOT)
    (PATCH_ROOT / "patches").mkdir(parents=True)

    records: list[dict[str, object]] = []
    required: dict[str, dict[str, object]] = {}
    vita_files = sorted(p for p in DEV_ROOT.rglob("*") if p.is_file())
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
                mode = "patch"

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
                if source:
                    shutil.copyfile(source, old_path)
                else:
                    old_path.write_bytes(b"")
                bsdiff4.file_diff(str(old_path), str(prepared_output), str(PATCH_ROOT / "patches" / patch_name))
                record["patch"] = patch_name
            records.append(record)
            if (number + 1) % 25 == 0 or number + 1 == len(vita_files):
                print(f"  processed {number + 1}/{len(vita_files)}")

    manifest = {
        "format": 3, "game": "DELTARUNE", "port_version": PORT_VERSION,
        "output_folder": "deltarune", "steam_folder": "DELTARUNE",
        "optimizations": {"console_borders": "RGBA PNG 960x544"},
        "required_sources": required, "files": records,
    }
    (PATCH_ROOT / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (PATCH_ROOT / "manifest.js").write_text(
        "const manifestData = " + json.dumps(manifest, indent=2, ensure_ascii=False) + ";\n", encoding="utf-8"
    )
    patch_size = sum(p.stat().st_size for p in (PATCH_ROOT / "patches").glob("*"))
    print(f"Created {len(records)} records and {patch_size / 1024 / 1024:.1f} MiB of binary patches.")


if __name__ == "__main__":
    main()

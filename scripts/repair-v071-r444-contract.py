"""Repair RGBA4444 cache sets without re-encoding already valid BC3 files.

This is intentionally narrow: it only recreates missing R444 payloads, refreshes
headers whose data.win offsets changed, removes stale extra R444 files and then
reopens the complete R444/BC3 set with DeltaCache's validator.
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = (
    ROOT
    / "data/deltarune_builder/prepare_texture_cache_v0_3_6_bundle/source"
    / "prepare_texture_cache_v0_3_6.py"
)
WORK_ROOT = ROOT / ".tmp/v071-r444-repair"


def load_tool():
    spec = importlib.util.spec_from_file_location("deltacache_prepare", TOOL_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Nao foi possivel carregar {TOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TARGETS = (
    ("Original", ROOT / "data/prepared/deltarune/deltarunevita/chapter0"),
    ("Original", ROOT / "data/prepared/deltarune/deltarunevita/chapter1"),
    ("Original", ROOT / "data/prepared/deltarune/deltarunevita/chapter2"),
    ("Original", ROOT / "data/prepared/deltarune/deltarunevita/chapter3"),
    ("Original", ROOT / "data/prepared/deltarune/deltarunevita/chapter4"),
    ("Portuguese-BR", ROOT / "mods/Lang/Portuguese-BR/chapter0"),
    ("Portuguese-BR", ROOT / "mods/Lang/Portuguese-BR/chapter2"),
)


def chapter_number(path: Path) -> int:
    return int(path.name.removeprefix("chapter"))


def repair_target(tool, language: str, chapter_dir: Path) -> None:
    chapter = chapter_number(chapter_dir)
    data_win = chapter_dir / "data.win"
    cache_dir = chapter_dir / "texture-cache"
    pvr_dir = chapter_dir / "pvr"
    tx = tool.parse_txtr(data_win)
    required = [page for page in tx["pages"] if page["blobOffset"] != 0]
    required_names = {f"page_{page['page']:03d}" for page in required}
    actual = {path.stem for path in cache_dir.glob("page_*.r444")}
    missing = sorted(required_names - actual)
    extras = sorted(actual - required_names)

    invalid = []
    for page in required:
        path = cache_dir / f"page_{page['page']:03d}.r444"
        if not path.is_file():
            continue
        try:
            tool.validate_r444(chapter, page, path)
        except RuntimeError:
            invalid.append(page)

    for name in extras:
        (cache_dir / f"{name}.r444").unlink()

    # Header-only mismatches are safe to repair without recompressing pixels.
    payload_refresh = []
    for page in invalid:
        path = cache_dir / f"page_{page['page']:03d}.r444"
        blob = path.read_bytes()
        expected_size = page["width"] * page["height"] * 2
        if len(blob) == 20 + expected_size:
            header = struct.pack(
                "<IIIII",
                tool.magic(chapter),
                page["blobSize"],
                page["blobOffset"],
                page["width"],
                page["height"],
            )
            path.write_bytes(header + blob[20:])
        else:
            payload_refresh.append(f"page_{page['page']:03d}")

    need_export = sorted(set(missing + payload_refresh))
    if need_export:
        work = WORK_ROOT / language / chapter_dir.name
        if work.exists():
            shutil.rmtree(work)
        rows = tool.export_pages(data_win, work)
        rows_by_page = {int(row["page"]): row for row in rows}
        pages_by_name = {f"page_{page['page']:03d}": page for page in required}
        for name in need_export:
            page = pages_by_name[name]
            row = rows_by_page.get(page["page"])
            if row is None:
                raise RuntimeError(f"{language}/{chapter_dir.name}/{name}: export ausente")
            if (int(row["width"]), int(row["height"])) != (page["width"], page["height"]):
                raise RuntimeError(f"{language}/{chapter_dir.name}/{name}: dimensoes divergentes")
            raw = work / "rgba4444" / f"{name}.rgba4444"
            tool.write_r444(chapter, page, raw, cache_dir / f"{name}.r444")

    disk_ids, external = tool.validate_physical_cache_set(
        chapter, tx, cache_dir, pvr_dir
    )
    tool.write_complete(chapter, tx["count"], cache_dir / "complete.vtc")

    manifest_path = chapter_dir / "texture_manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["r444_pages"] = disk_ids
        manifest["bc3_pages"] = disk_ids
        manifest["external_pages"] = external
        manifest.setdefault("validation", {})["r444"] = "PHYSICAL_OK_REPAIRED"
        manifest["validation"]["bc3"] = "PHYSICAL_OK_SRGB_PRESERVED"
        manifest["validation"]["complete_vtc"] = "WRITTEN_AFTER_PHYSICAL_AUDIT"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(
        f"OK {language}/{chapter_dir.name}: pages={tx['count']} "
        f"missing={len(missing)} refreshed={len(invalid)} extras={len(extras)}"
    )


def main() -> int:
    tool = load_tool()
    WORK_ROOT.mkdir(parents=True, exist_ok=True)
    for language, chapter_dir in TARGETS:
        repair_target(tool, language, chapter_dir)
    print("SUMMARY repaired=7 failed=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

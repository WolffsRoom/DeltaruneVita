"""Read-only audit of v0.71 data.win/R444/BC3 cache contracts."""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PREPARE = (
    ROOT
    / "data/deltarune_builder/prepare_texture_cache_v0_3_6_bundle/source"
    / "prepare_texture_cache_v0_3_6.py"
)


def load_prepare_module():
    spec = importlib.util.spec_from_file_location("deltacache_contract", PREPARE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {PREPARE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def candidate_roots() -> list[tuple[str, Path]]:
    roots = [("Original", ROOT / "data/prepared/deltarune/deltarunevita")]
    language_root = ROOT / "mods/Lang"
    if language_root.is_dir():
        for path in sorted(language_root.iterdir(), key=lambda item: item.name.lower()):
            if path.is_dir() and path.name != "icons" and "Pendente" not in path.name:
                roots.append((path.name, path))
    return roots


def main() -> int:
    cache = load_prepare_module()
    failures: list[str] = []
    checked = 0
    for variant, root in candidate_roots():
        for chapter_dir in sorted(root.glob("chapter[0-5]"), key=lambda item: item.name):
            win = chapter_dir / "data.win"
            r444 = chapter_dir / "texture-cache"
            bc3 = chapter_dir / "pvr"
            if not win.is_file():
                continue
            label = f"{variant}/{chapter_dir.name}"
            try:
                chapter = int(chapter_dir.name.removeprefix("chapter"))
                txtr = cache.parse_txtr(win)
                cache.validate_physical_cache_set(chapter, txtr, r444, bc3)
                complete = r444 / "complete.vtc"
                data = complete.read_bytes()
                if len(data) != 8:
                    raise RuntimeError("complete.vtc has an invalid size")
                magic, count = struct.unpack("<II", data)
                if magic != cache.complete_magic(chapter) or count != txtr["count"]:
                    raise RuntimeError(
                        f"complete.vtc mismatch magic=0x{magic:08X} count={count}; "
                        f"expected magic=0x{cache.complete_magic(chapter):08X} "
                        f"count={txtr['count']}"
                    )
                checked += 1
                print(f"OK   {label}: {txtr['count']} Texture Pages")
            except Exception as exc:  # report every package instead of stopping early
                failures.append(f"FAIL {label}: {exc}")
                print(failures[-1])
    print(f"SUMMARY checked={checked} failed={len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

import argparse
import concurrent.futures
import importlib.util
import json
import re
import shutil
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
DELTACACHE = PROJECT / "data" / "deltarune_builder" / "prepare_texture_cache_v0_3_6_bundle" / "source" / "prepare_texture_cache_v0_3_6.py"
WORK_ROOT = PROJECT / ".tmp" / "bc3-srgb-regeneration"
DEFAULT_ROOTS = [
    PROJECT / "data" / "prepared" / "deltarune" / "deltarunevita",
    PROJECT / "mods" / "Lang" / "German",
    PROJECT / "mods" / "Lang" / "Portuguese-BR",
    PROJECT / "mods" / "Lang" / "Russian",
    PROJECT / "mods" / "Lang" / "Spanish",
    PROJECT / "mods" / "Lang" / "Turkish",
]


def load_deltacache():
    spec = importlib.util.spec_from_file_location("deltacache_r2", DELTACACHE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def chapter_number(path: Path):
    match = re.fullmatch(r"chapter\s*(\d+)(?:_windows)?", path.name, re.IGNORECASE)
    return int(match.group(1)) if match else None


def discover(root: Path):
    found = []
    for data_win in root.rglob("data.win"):
        number = chapter_number(data_win.parent)
        if number is not None:
            found.append((number, data_win))
    return sorted(found, key=lambda item: item[0])


def update_manifest(chapter_dir: Path):
    manifest_path = chapter_dir / "texture_manifest.json"
    if not manifest_path.is_file():
        return
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    manifest["tool_version"] = "0.3.6-r2"
    manifest["bc3_encoding"] = {
        "format": "BC3/DXT5",
        "channel_type": "UBN",
        "input_colorspace": "sRGB",
        "output_colorspace": "sRGB",
        "gamma_conversion": False,
        "command": "-ics sRGB -f BC3,UBN,sRGB -m 1",
    }
    validation = manifest.setdefault("validation", {})
    validation["bc3"] = "PHYSICAL_OK_SRGB_PRESERVED"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def regenerate_chapter(dc, label: str, chapter: int, data_win: Path, workers: int):
    chapter_dir = data_win.parent
    pvr_dir = chapter_dir / "pvr"
    pvr_dir.mkdir(parents=True, exist_ok=True)
    work = WORK_ROOT / label / f"chapter{chapter}"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    txtr = dc.parse_txtr(data_win)
    rows = dc.export_pages(data_win, work)
    rows_by_page = {int(row["page"]): row for row in rows}
    required = [page for page in txtr["pages"] if page["blobOffset"] != 0]
    if len(rows) != txtr["count"]:
        raise RuntimeError(f"{label}/chapter{chapter}: exportou {len(rows)} de {txtr['count']} paginas")

    def convert(page):
        page_id = page["page"]
        base = f"page_{page_id:03d}"
        row = rows_by_page.get(page_id)
        if row is None:
            raise RuntimeError(f"{label}/chapter{chapter}/{base}: PNG ausente no indice")
        if (int(row["width"]), int(row["height"])) != (page["width"], page["height"]):
            raise RuntimeError(f"{label}/chapter{chapter}/{base}: dimensoes divergentes")
        png = work / "png" / f"{base}.png"
        if not png.is_file():
            raise RuntimeError(f"{label}/chapter{chapter}/{base}: PNG nao exportado")
        temporary = pvr_dir / f".{base}.bc3.tmp.pvr"
        final = pvr_dir / f"{base}.bc3.pvr"
        if temporary.exists():
            temporary.unlink()
        dc.make_bc3(png, temporary)
        dc.validate_pvr(page, temporary)
        temporary.replace(final)
        return page_id

    completed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(convert, page): page for page in required}
        for future in concurrent.futures.as_completed(futures):
            future.result()
            completed += 1
            if completed % 50 == 0 or completed == len(required):
                print(f"PROGRESS {label} chapter{chapter}: {completed}/{len(required)}", flush=True)

    expected = {f"page_{page['page']:03d}.bc3.pvr" for page in required}
    actual = {path.name for path in pvr_dir.glob("page_*.bc3.pvr")}
    # Remove obsolete cache entries for placeholders/external pages. Keeping
    # them lets an old atlas be selected after a translated data.win changes.
    for obsolete in actual - expected:
        (pvr_dir / obsolete).unlink()
    actual = {path.name for path in pvr_dir.glob("page_*.bc3.pvr")}
    if expected != actual:
        raise RuntimeError(
            f"{label}/chapter{chapter}: conjunto BC3 divergente "
            f"(missing={len(expected-actual)}, extra={len(actual-expected)})"
        )
    for page in required:
        dc.validate_pvr(page, pvr_dir / f"page_{page['page']:03d}.bc3.pvr")
    update_manifest(chapter_dir)
    shutil.rmtree(work)
    print(f"DONE {label} chapter{chapter}: {len(required)} BC3 sRGB", flush=True)
    return len(required)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--root", action="append", type=Path)
    parser.add_argument("--chapter", action="append", type=int,
                        help="Regenerate only the selected chapter number; may be repeated")
    args = parser.parse_args()
    dc = load_deltacache()
    roots = [path.resolve() for path in args.root] if args.root else DEFAULT_ROOTS
    total = 0
    WORK_ROOT.mkdir(parents=True, exist_ok=True)
    for root in roots:
        if not root.is_dir():
            raise RuntimeError(f"Raiz ausente: {root}")
        label = "Original" if root == DEFAULT_ROOTS[0] else root.name
        chapters = discover(root)
        if args.chapter:
            selected = set(args.chapter)
            chapters = [item for item in chapters if item[0] in selected]
        if not chapters:
            raise RuntimeError(f"Nenhum data.win encontrado: {root}")
        print(f"START {label}: {len(chapters)} chapters", flush=True)
        for chapter, data_win in chapters:
            total += regenerate_chapter(dc, label, chapter, data_win, max(1, args.workers))
        print(f"LANGUAGE_DONE {label}", flush=True)
    print(f"ALL_DONE total={total}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"FAILED: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)

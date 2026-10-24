#!/usr/bin/env python3
"""Refresh cache metadata after a GML-only Chapter 2 data.win rewrite.

The RGBA4444 payload and BC3 payload remain valid when only CODE/STRG move, but
VTC1 headers bind each R444 page to its absolute TXTR blob offset and size.
This tool updates those fields atomically, validates every physical cache file,
and writes complete.vtc only after the audit succeeds.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

VTC1 = 0x31435456
VTC2 = 0x32435456
PVR3 = 0x03525650
BC3 = 11
SRGB = 1


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def parse_txtr(path: Path) -> list[dict[str, int | bool]]:
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != b"FORM":
        raise RuntimeError("FORM header not found")
    pos = 8
    start = end = None
    while pos + 8 <= len(data):
        size = u32(data, pos + 4)
        chunk_start = pos + 8
        chunk_end = chunk_start + size
        if chunk_end > len(data):
            raise RuntimeError("chunk exceeds file")
        if data[pos : pos + 4] == b"TXTR":
            start, end = chunk_start, chunk_end
            break
        pos = chunk_end
    if start is None or end is None:
        raise RuntimeError("TXTR not found")

    count = u32(data, start)
    ptrs = [u32(data, start + 4 + i * 4) for i in range(count)]
    pages: list[dict[str, int | bool]] = []
    for page_id, ptr in enumerate(ptrs):
        if ptr == 0:
            pages.append({"id": page_id, "external": True, "width": 0,
                          "height": 0, "offset": 0, "size": 0,
                          "boundary": 0})
            continue
        if ptr + 28 > len(data):
            raise RuntimeError(f"page {page_id}: TXTR entry outside file")
        width, height = struct.unpack_from("<ii", data, ptr + 12)
        offset = u32(data, ptr + 24)
        if width == 0 and height == 0:
            pages.append({"id": page_id, "external": True, "width": 0,
                          "height": 0, "offset": 0, "size": 0,
                          "boundary": offset})
            continue
        if not (1 <= width <= 4096 and 1 <= height <= 4096):
            raise RuntimeError(f"page {page_id}: invalid {width}x{height}")
        pages.append({"id": page_id, "external": offset == 0,
                      "width": width, "height": height, "offset": offset,
                      "size": 0, "boundary": offset})

    for index, page in enumerate(pages):
        offset = int(page["offset"])
        if not offset:
            continue
        stop = end
        for following in pages[index + 1 :]:
            boundary = int(following["boundary"])
            if boundary:
                stop = boundary
                break
        if stop <= offset:
            raise RuntimeError(f"page {page['id']}: invalid blob range")
        page["size"] = stop - offset
    return pages


def validate_bc3(path: Path, width: int, height: int) -> None:
    data = path.read_bytes()
    if len(data) < 52:
        raise RuntimeError(f"{path.name}: short PVR")
    ver, flags, pixel_format, colorspace, channel_type, h, w, depth, surfaces, faces, mips, metadata = struct.unpack_from(
        "<IIQIIIIIIIII", data, 0
    )
    payload = len(data) - 52 - metadata
    expected = width * height
    if (ver, flags, pixel_format, colorspace, channel_type) != (PVR3, 0, BC3, SRGB, 0):
        raise RuntimeError(f"{path.name}: invalid BC3/sRGB header")
    if (w, h, depth, surfaces, faces, mips, payload) != (width, height, 1, 1, 1, 1, expected):
        raise RuntimeError(f"{path.name}: invalid dimensions/payload")


def refresh(chapter: Path) -> tuple[int, int]:
    pages = parse_txtr(chapter / "data.win")
    required = [page for page in pages if not page["external"]]
    cache = chapter / "texture-cache"
    pvr = chapter / "pvr"
    expected = {f"page_{int(page['id']):03d}" for page in required}
    found_r444 = {item.stem for item in cache.glob("page_*.r444")}
    found_bc3 = {item.name[: -len(".bc3.pvr")] for item in pvr.glob("page_*.bc3.pvr")}
    if found_r444 != expected or found_bc3 != expected:
        raise RuntimeError(
            f"physical set mismatch: expected={len(expected)} "
            f"r444={len(found_r444)} bc3={len(found_bc3)}"
        )

    changed = 0
    for page in required:
        page_id = int(page["id"])
        width, height = int(page["width"]), int(page["height"])
        offset, size = int(page["offset"]), int(page["size"])
        r444 = cache / f"page_{page_id:03d}.r444"
        raw = r444.read_bytes()
        if len(raw) != 20 + width * height * 2:
            raise RuntimeError(f"{r444.name}: invalid payload size")
        new_header = struct.pack("<IIIII", VTC1, size, offset, width, height)
        if raw[:20] != new_header:
            temporary = r444.with_suffix(r444.suffix + ".tmp")
            temporary.write_bytes(new_header + raw[20:])
            temporary.replace(r444)
            changed += 1
        validate_bc3(pvr / f"page_{page_id:03d}.bc3.pvr", width, height)

    complete = cache / "complete.vtc"
    temporary = complete.with_suffix(complete.suffix + ".tmp")
    temporary.write_bytes(struct.pack("<II", VTC2, len(pages)))
    temporary.replace(complete)
    return changed, len(required)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("chapters", nargs="+", type=Path)
    args = parser.parse_args()
    for chapter in args.chapters:
        changed, checked = refresh(chapter.resolve())
        print(f"OK {chapter}: reheadered={changed} validated={checked}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

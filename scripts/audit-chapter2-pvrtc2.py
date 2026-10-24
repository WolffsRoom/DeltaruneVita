#!/usr/bin/env python3
"""Compare Chapter 2 PVRTC2 pages with their lossless exported atlases."""

from __future__ import annotations

import csv
import re
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
PVR_TOOL = Path(r"C:\Program Files\Imgtec\PowerVR_Tools\PVRTexTool\CLI\Windows_x86_64\PVRTexToolCLI.exe")
PVR_DIR = ROOT / "data" / "pvr" / "chapter2"
SOURCE_DIR = ROOT / "data" / "pvr-work" / "chapter2-2048"
AUDIT_DIR = ROOT / "data" / "pvr-work" / "chapter2-pvrtc2-audit"
REPORT = AUDIT_DIR / "chapter2-pvrtc2-audit.csv"
CONTENTS = ROOT / "data" / "pvr-work" / "chapter2-page-contents.txt"


def mixed_pages() -> set[str]:
    mixed: set[str] = set()
    if not CONTENTS.exists():
        return mixed
    with CONTENTS.open(encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream, delimiter="|"):
            sprites = [name for name in row.get("sprites", "").split(",") if name]
            if any(not name.lower().startswith("bg_") for name in sprites):
                mixed.add(f"page_{int(row['page']):03d}")
    return mixed


def decode(pvr: Path, output: Path) -> None:
    subprocess.run(
        [str(PVR_TOOL), "-i", str(pvr), "-d", str(output)],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
    )


def metrics(source: Path, decoded: Path) -> dict[str, object]:
    original = np.asarray(Image.open(source).convert("RGBA"), dtype=np.int16)
    compressed = np.asarray(Image.open(decoded).convert("RGBA"), dtype=np.int16)
    if original.shape != compressed.shape:
        return {"decision": "RGBA4444", "reason": "dimension_mismatch"}

    alpha = original[:, :, 3]
    visible = alpha > 0
    edge = (alpha > 0) & (alpha < 255)
    rgb_error = np.abs(original[:, :, :3] - compressed[:, :, :3]).max(axis=2)
    alpha_error = np.abs(alpha - compressed[:, :, 3])
    visible_errors = rgb_error[visible]
    edge_errors = rgb_error[edge]
    alpha_flip = np.count_nonzero((alpha == 0) != (compressed[:, :, 3] == 0))
    visible_count = max(1, int(np.count_nonzero(visible)))
    edge_count = max(1, int(np.count_nonzero(edge)))

    mean_rgb = float(visible_errors.mean()) if visible_errors.size else 0.0
    p95_rgb = float(np.percentile(visible_errors, 95)) if visible_errors.size else 0.0
    edge_mean = float(edge_errors.mean()) if edge_errors.size else 0.0
    alpha_mean = float(alpha_error[visible].mean()) if visible_errors.size else 0.0
    flip_percent = 100.0 * alpha_flip / alpha.size
    edge_percent = 100.0 * edge_count / visible_count

    # Pixel art and tightly packed sprite sheets are rejected aggressively.
    # A scenery page may tolerate small block error, but alpha topology must
    # remain stable or neighboring atlas cells bleed into one another.
    unsafe = (
        p95_rgb > 48.0
        or edge_mean > 34.0
        or alpha_mean > 10.0
        or flip_percent > 0.02
        or edge_percent > 8.0
    )
    reasons = []
    if p95_rgb > 48.0: reasons.append("color_error")
    if edge_mean > 34.0: reasons.append("edge_bleed")
    if alpha_mean > 10.0: reasons.append("alpha_error")
    if flip_percent > 0.02: reasons.append("alpha_topology")
    if edge_percent > 8.0: reasons.append("dense_transparency")
    return {
        "decision": "RGBA4444" if unsafe else "PVRTC2",
        "reason": ",".join(reasons) if reasons else "safe_scenery",
        "mean_rgb_error": f"{mean_rgb:.3f}",
        "p95_rgb_error": f"{p95_rgb:.3f}",
        "edge_mean_error": f"{edge_mean:.3f}",
        "alpha_mean_error": f"{alpha_mean:.3f}",
        "alpha_flip_percent": f"{flip_percent:.6f}",
        "edge_visible_percent": f"{edge_percent:.3f}",
    }


def main() -> None:
    AUDIT_DIR.mkdir(parents=True, exist_ok=True)
    rows = []
    mixed = mixed_pages()
    for pvr in sorted(PVR_DIR.glob("page_*.pvr")):
        if not re.fullmatch(r"page_\d{3}", pvr.stem):
            continue
        source = SOURCE_DIR / f"{pvr.stem}.png"
        decoded = AUDIT_DIR / f"{pvr.stem}_pvrtc2.png"
        if not source.exists():
            rows.append({"page": pvr.stem, "decision": "RGBA4444", "reason": "missing_source"})
            continue
        decode(pvr, decoded)
        row = {"page": pvr.stem, **metrics(source, decoded)}
        if pvr.stem in mixed:
            row["decision"] = "RGBA4444"
            row["reason"] = "mixed_dynamic_atlas"
        rows.append(row)

    fields = [
        "page", "decision", "reason", "mean_rgb_error", "p95_rgb_error",
        "edge_mean_error", "alpha_mean_error", "alpha_flip_percent",
        "edge_visible_percent",
    ]
    with REPORT.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(REPORT)
    for row in rows:
        print(row["page"], row["decision"], row["reason"])


if __name__ == "__main__":
    main()

from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/current/Old Brothers/Deltarune-v0.80-59.vpk"
COMPILED = ROOT / "artifacts/current/Deltarune-v0.81-3.vpk"
TARGET = ROOT / "artifacts/current/Deltarune-v0.81-4.vpk"
BORDER_SOURCE = ROOT / "data/mods/bordersanim"
BETTER_SOURCE = ROOT / "mods/Borders"
BORDER_ZIP = ROOT / "artifacts/current/Deltarune-Animated-Borders-v0.81.zip"

assert BASE.exists() and COMPILED.exists()
with zipfile.ZipFile(COMPILED) as compiled:
    assert compiled.testzip() is None
    overrides = {
        "gxm.bin": compiled.read("gxm.bin"),
        "vitagl.bin": compiled.read("vitagl.bin"),
        "assets/build_version.txt": b"DELTARUNE VITA v0.81\n",
    }
with zipfile.ZipFile(BASE) as old, zipfile.ZipFile(TARGET, "w") as new:
    for item in old.infolist():
        new.writestr(item, overrides.pop(item.filename, old.read(item.filename)))
    assert not overrides

with zipfile.ZipFile(BASE) as old, zipfile.ZipFile(TARGET) as new:
    assert new.testzip() is None
    assert new.namelist() == old.namelist()
    with zipfile.ZipFile(COMPILED) as compiled:
        assert new.read("gxm.bin") == compiled.read("gxm.bin")
        assert new.read("vitagl.bin") == compiled.read("vitagl.bin")
    assert new.read("assets/build_version.txt") == b"DELTARUNE VITA v0.81\n"
    modified = [name for name in old.namelist() if old.read(name) != new.read(name)]
    assert set(modified) == {"gxm.bin", "vitagl.bin", "assets/build_version.txt"}, modified

border_entries = []
with zipfile.ZipFile(BORDER_ZIP, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as out:
    readme = (
        "DELTARUNE Vita v0.81 - Animated Borders\n\n"
        "Extract this archive inside ux0:data/deltarune/deltarunevita/mods/.\n"
        "The Both/Ambos setting enables the animated Chapter 1 fan-made borders.\n"
    )
    out.writestr("README.txt", readme)
    for source, prefix in ((BORDER_SOURCE, "bordersanim"), (BETTER_SOURCE, "Borders")):
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(source).as_posix()
            arcname = f"{prefix}/{relative}"
            out.write(path, arcname)
            border_entries.append(arcname)

with zipfile.ZipFile(BORDER_ZIP) as borders:
    assert borders.testzip() is None
    for required in (
        "Borders/border1.png", "Borders/border2.png", "Borders/border3.png",
        "bordersanim/border1/frames_side.bani",
        "bordersanim/border2/frames_side.bani",
        "bordersanim/border3/frames_side.bani",
        "bordersanim/border_dw_titan_eyes/frames_side.bani",
        "bordersanim/border_dw_titan_eyes_red/frames_side.bani",
    ):
        assert required in borders.namelist(), required

manifest = {
    "version": "v0.81",
    "vpk_path": str(TARGET.relative_to(ROOT)),
    "vpk_size_bytes": TARGET.stat().st_size,
    "vpk_sha256": hashlib.sha256(TARGET.read_bytes()).hexdigest(),
    "base_vpk": str(BASE.relative_to(ROOT)),
    "modified_vpk_files": modified,
    "border_pack": str(BORDER_ZIP.relative_to(ROOT)),
    "border_pack_size_bytes": BORDER_ZIP.stat().st_size,
    "border_pack_sha256": hashlib.sha256(BORDER_ZIP.read_bytes()).hexdigest(),
    "border_pack_entries": len(border_entries) + 1,
    "compile": "PASS (VitaSDK softfp); v0.80-59 package preserved, v0.81 GXM and VitaGL runners installed",
    "runtime_validation": "PS Vita/Vita3K pending",
}
manifest_path = ROOT / "artifacts/testing/v0.81-manifest.json"
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(manifest, ensure_ascii=False, indent=2))

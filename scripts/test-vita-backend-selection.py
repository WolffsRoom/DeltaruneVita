from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/vita-common/vita_backend_selection.h"
SOURCE = ROOT / "src/vita-common/vita_backend_selection.c"

assert HEADER.exists(), "missing vita_backend_selection.h"
assert SOURCE.exists(), "missing vita_backend_selection.c"

text = HEADER.read_text(encoding="utf-8")
for token in (
    "VITA_BACKEND_VITAGL",
    "VITA_BACKEND_GXM",
    "VitaBackendSelection_parse",
    "VitaBackendSelection_executable",
    "VitaBackendSelection_resolve",
    '"app0:/vitagl.bin"',
    '"app0:/gxm.bin"',
):
    assert token in text, f"missing contract token: {token}"

source = SOURCE.read_text(encoding="utf-8")
assert 'valueEquals(value, "gxm") ? VITA_BACKEND_GXM' in source
assert "return VITA_BACKEND_VITAGL" in source
assert "if (requested == VITA_BACKEND_GXM && gxmAvailable)" in source
assert "if (vitaGlAvailable) return VITA_BACKEND_VITAGL" in source
assert "return VITA_BACKEND_UNAVAILABLE" in source
assert 'snprintf(temporary, sizeof(temporary), "%s.tmp", path)' in source
assert "rename(temporary, path)" in source

print("Vita backend selection contract: PASS")

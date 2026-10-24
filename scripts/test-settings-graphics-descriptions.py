from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/vita-probe/source/vita_settings.c").read_text(encoding="utf-8")

assert "drawCenteredUpperTextFitted" in source
assert "TextUtils_measureLineWidth" in source
assert "maxWidth" in source

expected = (
    "Selects the safest renderer for the current chapter.",
    "Uses the stable fixed-function renderer.",
    "Uses the complete programmable shader renderer.",
    "Combines the stable renderer with adapted shaders.",
)
for text in expected:
    assert text in source, f"missing renderer help: {text}"

assert "pendingRendererMode == VITA_RENDERER_AUTOMATIC" in source
assert "pendingRendererMode == VITA_RENDERER_LEGACY" in source
assert "pendingRendererMode == VITA_RENDERER_MODERN" in source
assert "pendingRendererMode == VITA_RENDERER_EXPERIMENTAL" in source
assert "drawCenteredUpperTextFitted(r, desc, 480, 390" in source

print("graphics settings descriptions: PASS")

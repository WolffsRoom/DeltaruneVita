from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src/vita-launcher/main.c"
UI = ROOT / "src/vita-launcher/launcher_ui.c"
CMAKE = ROOT / "src/vita-launcher/CMakeLists.txt"

for path in (MAIN, UI, CMAKE):
    assert path.exists(), f"missing launcher file: {path}"

main = MAIN.read_text(encoding="utf-8")
ui = UI.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

for token in (
    "VITA_LAUNCHER_COUNTDOWN_US",
    "SCE_CTRL_LTRIGGER",
    "SCE_CTRL_RTRIGGER",
    "VITA_BACKEND_GXM_BOOT_MARKER",
    "VitaBackendSelection_resolve",
    "VitaBackendSelection_save",
    "sceAppMgrLoadExec",
    "LAUNCH_RESULT_MISSING_VITAGL",
    "LAUNCH_RESULT_GXM_FALLBACK",
):
    assert token in main, f"missing launcher state token: {token}"

assert "vitaGL" not in main + ui
assert "sceGxmInitialize" not in main + ui
assert "SceGxm_stub" not in cmake
assert "vitaGL" not in cmake
assert "SceDisplay_stub" in cmake
assert "SceCtrl_stub" in cmake
assert "SceAppMgr_stub" in cmake
assert "VitaGL - Estavel" in ui
assert "Native-GXM - Experimental" in ui
assert "Falha ao iniciar" in main + ui

print("Vita renderer launcher contract: PASS")

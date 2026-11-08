from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
main=(ROOT/"src/vita-launcher/main.c").read_text(encoding="utf-8")
cmake=(ROOT/"src/vita-probe/CMakeLists.txt").read_text(encoding="utf-8")
assert "VITA_BACKEND_VITAGL" in main
assert "VitaBackendSelection_executable(backend)" in main
assert "sceAppMgrLoadExec(executable,NULL,NULL)" in main
assert "VitaBackendSelection_save(VITA_BACKEND_PREFERENCE_PATH,backend)" in main
assert "VITA_BACKEND_GXM_BOOT_MARKER" not in main
assert "LauncherUi_draw" not in main and "SCE_CTRL_LTRIGGER" not in main
assert "psvDebugScreenInit();" in main  # errors only, no startup UI
assert 'VITA_BACKEND_GXM_EXECUTABLE' in (ROOT/"src/vita-common/vita_backend_selection.h").read_text(encoding="utf-8")
assert "SceAppMgr_stub" in cmake
assert "SceDisplay_stub" in cmake
print("VitaGL direct-launch / no graphics selector contract: PASS")

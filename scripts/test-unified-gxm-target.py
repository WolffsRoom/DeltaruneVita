from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
native_cmake = (ROOT / "experiments/native-gxm/CMakeLists.txt").read_text(encoding="utf-8")
main_cmake = (ROOT / "src/vita-probe/CMakeLists.txt").read_text(encoding="utf-8")
build_script = (ROOT / "scripts/build-butterscotch-probe.ps1").read_text(encoding="utf-8")
native_main = (ROOT / "experiments/native-gxm/main.c").read_text(encoding="utf-8")

assert "option(GXM_UNIFIED_BUILD" in native_cmake
assert "PORT_BUILD_VERSION" in native_cmake
assert "vita_create_self(gxm.bin" in native_cmake
assert "if(NOT GXM_UNIFIED_BUILD)" in native_cmake
assert "vitaGL" not in "\n".join(
    line for line in native_cmake.splitlines() if "target_link_libraries" in line
)

assert "GXM_SELF_PATH" in main_cmake
assert '"${GXM_SELF_PATH}" gxm.bin' in main_cmake

assert "GXM_UNIFIED_BUILD=ON" in build_script
assert "NANOVG_GXM_SOURCE_DIR" in build_script
assert "GXM_SELF_PATH" in build_script

assert "BACKEND=native-gxm executable=app0:/gxm.bin" in native_main
assert "VITA_BACKEND_GXM_BOOT_MARKER" in native_main
assert "gxm_boot_marker_create" in native_main
assert "gxm_boot_marker_clear" in native_main
assert "app0:eboot.bin" in native_main

print("unified GXM target contract: PASS")

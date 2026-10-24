from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "src/vita-probe/CMakeLists.txt").read_text(encoding="utf-8")
MAIN = (ROOT / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
BUILD = (ROOT / "scripts/build-butterscotch-probe.ps1").read_text(encoding="utf-8")

assert "vita_create_self(vitagl.bin ${PROJECT_NAME} UNSAFE)" in CMAKE
assert "add_executable(deltarune_renderer_launcher" in CMAKE
assert "vita_create_self(eboot.bin deltarune_renderer_launcher UNSAFE)" in CMAKE
assert '"${CMAKE_BINARY_DIR}/vitagl.bin" vitagl.bin' in CMAKE
assert "add_dependencies(Deltarune.vpk-vpk vitagl.bin-self)" in CMAKE
assert "BACKEND=vitagl executable=app0:/vitagl.bin" in MAIN
assert "Deltarune.vpk" in BUILD

# The stable runner retains all major source groups and VitaGL dependencies.
for token in ("${BS_COMMON_SOURCES}", "${BS_GL_SOURCES}",
              "libvitaGL.a", "vitashark", "SceShaccCgExt"):
    assert token in CMAKE, f"VitaGL runner lost dependency: {token}"

print("Unified VitaGL target contract: PASS")

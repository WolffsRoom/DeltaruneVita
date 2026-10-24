from pathlib import Path


root = Path(__file__).resolve().parents[1]
main = (root / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
trophies = (root / "src/vita-probe/source/vita_trophies.c").read_text(encoding="utf-8")
legacy = (root / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")

early = main.split("bool nativeTrophies = VitaTrophies_init();", 1)[0]
assert "CHAPTER_LOADING=visible phase=pre_trophies_datawin" in early
assert "loading_screen_init();" in early
assert "LOADING_LABEL_LOADING_TEXTURES" in early

progress = main.split("static void progress", 1)[1].split("static void draw_loading_screen", 1)[0]
assert "loading_overlay_current = index + 1" in progress
assert "loading_overlay_total = total" in progress
assert "draw_loading_screen(true" in progress

assert "VitaProbe_pumpLoadingScreen();" in trophies
dialog = trophies.split("dialog_loop_enter", 1)[1].split("dialog_loop_exit", 1)[0]
assert "glClear(GL_COLOR_BUFFER_BIT)" not in dialog

eviction = legacy.split("static bool vitaEvictTexturePage", 1)[1]
eviction = eviction.split("if (smallPage)", 1)[0]
assert "gl->textureFrame - gl->textureLastEvictedFrame[i] <= 30U" in eviction

print("early chapter loading / Flowery cache contracts: PASS")

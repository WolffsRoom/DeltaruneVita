#!/usr/bin/env python3
"""Contract checks for Vita startup dependency and USB-video guards."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
VIDEO = (ROOT / "src/vita-probe/source/vita_video.c").read_text(encoding="utf-8")
HEADER = (ROOT / "src/vita-probe/source/vita_video.h").read_text(encoding="utf-8")


def require(text: str, source: str, description: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {description}: {text!r}")


require("ur0:data/libshacccg.suprx", MAIN, "official libshacccg path")
require("STARTUP_FATAL=libshacccg_missing", MAIN, "missing shader compiler marker")
require("show_shader_dependency_error_and_exit", MAIN, "shader-free dependency dialog")
require("ur0:tai/config.txt", MAIN, "ur0 tai config scan")
require("ux0:tai/config.txt", MAIN, "ux0 tai config scan")
for plugin in ("uac_uvc", "uvc_uac", "udcd_uvc"):
    require(plugin, MAIN, f"{plugin} detection")
for allowed in ('"vitagrafix"', '"vgi.suprx"'):
    if allowed in MAIN.lower():
        raise AssertionError(f"graphics plugin must not disable AvPlayer: {allowed}")
require("VIDEO_GUARD=usb_capture_plugin_detected", MAIN, "USB capture-plugin guard marker")
require("FIRST_LOAD_VIDEO=disabled_plugin_conflict", MAIN, "PNG fallback marker")
require("VitaVideo_setHardwareDisabled", HEADER, "video safety API")
require("VitaVideo_setHardwareDisabled", VIDEO, "video safety implementation")
require("video_disabled", VIDEO, "safe GML video-open result")

init_done = MAIN.index('log_line("VITAGL=init_complete")')
shader_guard = MAIN.index("STARTUP_FATAL=libshacccg_missing")
data_check = MAIN.index("verify_essential_data_or_report(active_chapter)")
if not init_done < shader_guard < data_check:
    raise AssertionError("libshacccg dialog must run after VitaGL init and before game-data loading")

dialog_start = MAIN.index("static void show_shader_dependency_error_and_exit")
dialog_end = MAIN.index("static bool g_initialRootExists", dialog_start)
dialog = MAIN[dialog_start:dialog_end]
for forbidden in ("loading_screen_init", "draw_loading_screen", "glDraw"):
    if forbidden in dialog:
        raise AssertionError(f"missing-lib dialog must stay shader-free: {forbidden}")

print("startup runtime guard contracts: ok")

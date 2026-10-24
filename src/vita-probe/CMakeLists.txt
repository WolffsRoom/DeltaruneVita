cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK nao definido")
endif()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
project(deltarune_butterscotch_probe C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Project version source. scripts/build-butterscotch-probe.ps1 supplies the
# complete internal build tag (for example v0.71-98) through PORT_BUILD_VERSION.
file(READ "${CMAKE_SOURCE_DIR}/../../config/version.txt" PORT_BASE_VERSION)
string(STRIP "${PORT_BASE_VERSION}" PORT_BASE_VERSION)
if(NOT PORT_BASE_VERSION MATCHES "^[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Invalid config/version.txt: ${PORT_BASE_VERSION}")
endif()
if(NOT DEFINED PORT_BUILD_VERSION OR PORT_BUILD_VERSION STREQUAL "")
  set(PORT_BUILD_VERSION "v${PORT_BASE_VERSION} (Direct Build)")
endif()
string(REPLACE "." ";" PORT_VERSION_PARTS "${PORT_BASE_VERSION}")
list(GET PORT_VERSION_PARTS 0 PORT_VERSION_MAJOR)
list(GET PORT_VERSION_PARTS 1 PORT_VERSION_MINOR)
string(LENGTH "${PORT_VERSION_MAJOR}" PORT_VERSION_MAJOR_LEN)
string(LENGTH "${PORT_VERSION_MINOR}" PORT_VERSION_MINOR_LEN)
if(PORT_VERSION_MAJOR_LEN EQUAL 1)
  set(PORT_VERSION_MAJOR "0${PORT_VERSION_MAJOR}")
endif()
if(PORT_VERSION_MINOR_LEN EQUAL 1)
  set(PORT_VERSION_MINOR "0${PORT_VERSION_MINOR}")
endif()
set(VITA_PACKAGE_VERSION "${PORT_VERSION_MAJOR}.${PORT_VERSION_MINOR}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfloat-abi=softfp")

option(PLAYABLE_RUNNER "Build full Butterscotch Vita runner" ON)
option(VITAGL_PROBE "Build isolated vitaGL diagnostic" OFF)
if(PLAYABLE_RUNNER)
  file(GLOB BS_COMMON_SOURCES ../butterscotch/src/*.c)
  file(GLOB BS_GL_SOURCES
    ../butterscotch/src/gl/*.c
      ../butterscotch/src/gl_common/*.c
    ../butterscotch/src/gl_legacy/*.c
    ../butterscotch/src/image/*.c
  )
    add_executable(${PROJECT_NAME}
    source/playable_main.c
    source/vita_settings.c
    source/vita_borders.c
    source/vita_border_rules.c
    source/vita_media_rules.c
    source/vita_gpu_rules.c
    source/vita_thread_policy.c
    source/vita_background_work.c
    source/vita_frame_pacer.c
    source/vita_startup_preload.c
    source/vita_room_prefetch.c
    source/vita_io_profile.c
    source/vita_video.c
      source/vita_trophies.c
    source/vita_network.c
    ../vita-common/vita_backend_selection.c
    ${BS_COMMON_SOURCES}
    ${BS_GL_SOURCES}
    ../butterscotch/src/audio/openal/al_audio_system.c
    ../butterscotch/src/audio/openal/wave.c
    ../butterscotch/src/android/stb_impl.c
    ../butterscotch/vendor/md5/md5.c
    ../butterscotch/vendor/sha1/sha1.c
    ../butterscotch/vendor/base64/base64.c
    ../butterscotch/vendor/bzip2/bz_internal_error.c
    ../butterscotch/vendor/bzip2/bzlib.c
    ../butterscotch/vendor/bzip2/crctable.c
    ../butterscotch/vendor/bzip2/decompress.c
    ../butterscotch/vendor/bzip2/huffman.c
    ../butterscotch/vendor/bzip2/randtable.c
  )
elseif(VITAGL_PROBE)
  add_executable(${PROJECT_NAME}
    source/vitagl_probe.c
    ../butterscotch/src/data_win.c
    ../butterscotch/src/binary_reader.c
    ../butterscotch/src/image/image_decoder.c
    ../butterscotch/src/android/stb_impl.c
    ../butterscotch/vendor/bzip2/bz_internal_error.c
    ../butterscotch/vendor/bzip2/bzlib.c
    ../butterscotch/vendor/bzip2/crctable.c
    ../butterscotch/vendor/bzip2/decompress.c
    ../butterscotch/vendor/bzip2/huffman.c
    ../butterscotch/vendor/bzip2/randtable.c
  )
else()
  add_executable(${PROJECT_NAME}
    source/main.c
    ../butterscotch/src/data_win.c
    ../butterscotch/src/binary_reader.c
    ../butterscotch/src/web-meta/stb_impl.c
    $ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common/debugScreen.c
  )
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(${PROJECT_NAME} PRIVATE
  source
  ../vita-common
  ../butterscotch/src
    ../butterscotch/src/gl
  ../butterscotch/vendor/stb/ds
  ../butterscotch/vendor/stb/image
  ../butterscotch/vendor/stb/image_write
  ../butterscotch/src/image
  ../butterscotch/src/gl_common
  ../butterscotch/src/gl_legacy
  ../butterscotch/src/audio/openal
  ../butterscotch/vendor/bzip2
  ../butterscotch/vendor/md5
  ../butterscotch/vendor/sha1
  ../butterscotch/vendor/base64
  ../butterscotch/vendor/stb/vorbis
  $ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  ENABLE_WAD17 ENABLE_LEGACY_GL PLATFORM_VITA ENABLE_VM_GML_PROFILER
  BUTTERSCOTCH_COMMIT_HASH="vita-port"
  BUTTERSCOTCH_COMMIT_DATE="2026-07-20"
  PORT_BUILD_VERSION="${PORT_BUILD_VERSION}"
)
target_link_libraries(${PROJECT_NAME}
  ${CMAKE_SOURCE_DIR}/../../third_party/vitaGL-nosplash/libvitaGL.a
  vitashark
  SceShaccCgExt
  taihen_stub
  SceShaccCg_stub
  SceAppMgr_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceKernelDmacMgr_stub
  ScePower_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  SceRtc_stub
  SceSysmem_stub
  SceTouch_stub
  kubridge_stub
  mathneon
  openal
  pthread
  SceAudio_stub
  SceAudioIn_stub
  SceAppUtil_stub
  SceNpTrophy_stub
  SceCommonDialog_stub
  SceAvPlayer_stub
  z
  m
)

vita_create_self(vitagl.bin ${PROJECT_NAME} UNSAFE)

# The primary executable is a framebuffer-only selector. Keep its target in
# this directory because vita_create_self resolves the ELF target locally.
add_executable(deltarune_renderer_launcher
  ../vita-launcher/main.c
  ../vita-launcher/launcher_ui.c
  ../vita-common/vita_backend_selection.c
  $ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common/debugScreen.c
)
target_include_directories(deltarune_renderer_launcher PRIVATE
  ../vita-launcher
  ../vita-common
  $ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common
)
target_link_libraries(deltarune_renderer_launcher
  SceAppMgr_stub
  SceCtrl_stub
  SceDisplay_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
)
vita_create_self(eboot.bin deltarune_renderer_launcher UNSAFE)

set(GXM_SELF_PATH "" CACHE FILEPATH "Prebuilt Native-GXM SELF for the unified VPK")
if(NOT GXM_SELF_PATH OR NOT EXISTS "${GXM_SELF_PATH}")
  message(FATAL_ERROR "Build Native-GXM first and pass -DGXM_SELF_PATH=<path>/gxm.bin")
endif()

# Language flags are optional.  Package every PNG present without requiring a
# CMake edit whenever a new external language is added.
file(GLOB LANGUAGE_ICON_SOURCES
  "${CMAKE_SOURCE_DIR}/../../mods/Lang/icons/*.png"
)
set(LANGUAGE_ICON_VPK_FILES)
foreach(LANGUAGE_ICON_SOURCE ${LANGUAGE_ICON_SOURCES})
  get_filename_component(LANGUAGE_ICON_NAME "${LANGUAGE_ICON_SOURCE}" NAME)
  list(APPEND LANGUAGE_ICON_VPK_FILES
    "${LANGUAGE_ICON_SOURCE}" "assets/ui/Lang/${LANGUAGE_ICON_NAME}"
  )
endforeach()

# Localized loading labels are compact atlases generated ahead of time. This
# avoids keeping three sparse 960x544 RGBA textures in memory.
file(GLOB LOADING_LANGUAGE_SOURCES
  "${CMAKE_SOURCE_DIR}/../../data/frames/Lang/*.png"
)
set(LOADING_LANGUAGE_VPK_FILES)
foreach(LOADING_LANGUAGE_SOURCE ${LOADING_LANGUAGE_SOURCES})
  get_filename_component(LOADING_LANGUAGE_NAME "${LOADING_LANGUAGE_SOURCE}" NAME)
  list(APPEND LOADING_LANGUAGE_VPK_FILES
    "${LOADING_LANGUAGE_SOURCE}" "assets/loading/Lang/${LOADING_LANGUAGE_NAME}"
  )
endforeach()

file(GLOB LOADING_MINIGAME_LANGUAGE_SOURCES
  "${CMAKE_SOURCE_DIR}/../../data/frames/LangMiniGame/*.png"
)
set(LOADING_MINIGAME_LANGUAGE_VPK_FILES)
foreach(LOADING_MINIGAME_LANGUAGE_SOURCE ${LOADING_MINIGAME_LANGUAGE_SOURCES})
  get_filename_component(LOADING_MINIGAME_LANGUAGE_NAME "${LOADING_MINIGAME_LANGUAGE_SOURCE}" NAME)
  list(APPEND LOADING_MINIGAME_LANGUAGE_VPK_FILES
    "${LOADING_MINIGAME_LANGUAGE_SOURCE}" "assets/loading/LangMiniGame/${LOADING_MINIGAME_LANGUAGE_NAME}"
  )
endforeach()

file(GLOB_RECURSE LOADING_ANIMATION_SOURCES
  "${CMAKE_SOURCE_DIR}/../../data/frames/animations/*.png"
)
set(LOADING_ANIMATION_VPK_FILES)
foreach(LOADING_ANIMATION_SOURCE ${LOADING_ANIMATION_SOURCES})
  file(RELATIVE_PATH LOADING_ANIMATION_RELATIVE
    "${CMAKE_SOURCE_DIR}/../../data/frames/animations"
    "${LOADING_ANIMATION_SOURCE}"
  )
  list(APPEND LOADING_ANIMATION_VPK_FILES
    "${LOADING_ANIMATION_SOURCE}"
    "assets/loading/animations/${LOADING_ANIMATION_RELATIVE}"
  )
endforeach()

vita_create_vpk(Deltarune.vpk DELT00001 eboot.bin
  VERSION ${VITA_PACKAGE_VERSION}
  NAME "Deltarune"
  FILE
    "${CMAKE_BINARY_DIR}/vitagl.bin" vitagl.bin
    "${GXM_SELF_PATH}" gxm.bin
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/Newest/icon0.png sce_sys/icon0.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/Newest/startup.png sce_sys/livearea/contents/startup.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/Newest/bg0.png sce_sys/livearea/contents/bg0.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/001.png sce_sys/manual/001.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/002.png sce_sys/manual/002.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/003.png sce_sys/manual/003.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/004.png sce_sys/manual/004.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/005.png sce_sys/manual/005.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/006.png sce_sys/manual/006.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/007.png sce_sys/manual/007.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/008.png sce_sys/manual/008.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_joybase_frame0.png assets/touch/spr_joybase_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_joystick_frame0.png assets/touch/spr_joystick_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_zkey_frame0.png assets/touch/spr_control_zkey_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_xkey_frame0.png assets/touch/spr_control_xkey_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_ckey_frame0.png assets/touch/spr_control_ckey_frame0.png
    ${LOADING_LANGUAGE_VPK_FILES}
    ${LOADING_MINIGAME_LANGUAGE_VPK_FILES}
    ${LOADING_ANIMATION_VPK_FILES}
    ${CMAKE_SOURCE_DIR}/../../data/frames/loadtoby.ogg assets/loading/loadtoby.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/animations/touch.ogg assets/loading/touch.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/snd_hurt1.ogg assets/loading/minigame_sounds/snd_hurt1.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/snd_graze.ogg assets/loading/minigame_sounds/snd_graze.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/snd_cardrive.ogg assets/loading/minigame_sounds/snd_cardrive.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/snd_won.ogg assets/loading/minigame_sounds/snd_won.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/play.ogg assets/loading/minigame_sounds/play.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/stop.ogg assets/loading/minigame_sounds/stop.ogg
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/Assets/spr_heart_0.png assets/loading/minigame/spr_heart_0.png
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/Assets/spinheartmask.png assets/loading/minigame/spinheartmask.png
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/Assets/projectile_diamond.png assets/loading/minigame/projectile_diamond.png
    ${CMAKE_SOURCE_DIR}/../../data/frames/mini_game/Assets/projectile_ball.png assets/loading/minigame/projectile_ball.png
    ${CMAKE_SOURCE_DIR}/../../data/frames/NewDeltaruneLoadIntro.mp4 first_load.mp4
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_circle_0.png assets/ui/button_ps4_circle_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_cross_0.png assets/ui/button_ps4_cross_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_triangle_0.png assets/ui/button_ps4_triangle_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_square_0.png assets/ui/button_ps4_square_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_l1.png assets/ui/button_ps4_l1.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_r1.png assets/ui/button_ps4_r1.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_options.png assets/ui/button_ps4_options.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_up.png assets/ui/button_ps4_dpad_up.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_down.png assets/ui/button_ps4_dpad_down.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_left.png assets/ui/button_ps4_dpad_left.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_right.png assets/ui/button_ps4_dpad_right.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_joyL.png assets/ui/button_psv_joyL.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_joyR.png assets/ui/button_psv_joyR.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_L.png assets/ui/button_psv_L.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_R.png assets/ui/button_psv_R.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_select.png assets/ui/button_psv_select.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_touchA.png assets/ui/button_psv_touchA.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/spr_textbox_topleft_0.png assets/ui/spr_textbox_topleft_0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_heart_0.png assets/ui/spr_heart_0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_darkheart_0.png assets/ui/spr_darkheart_0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_darkheart_1.png assets/ui/spr_darkheart_1.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_darkheart_2.png assets/ui/spr_darkheart_2.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_darkheart_3.png assets/ui/spr_darkheart_3.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/Trophies/spr_darkheart_4.png assets/ui/spr_darkheart_4.png
    ${CMAKE_SOURCE_DIR}/../../data/trophy/DELT00001_01/TROPHY.TRP sce_sys/trophy/DELT00001_01/TROPHY.TRP
    ${CMAKE_SOURCE_DIR}/../../data/assets/build_version.txt assets/build_version.txt
    ${CMAKE_SOURCE_DIR}/../../data/assets/sounds/snd-select.ogg assets/sounds/snd-select.ogg
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/sounds/snd_menumove.ogg assets/sounds/snd_menumove.ogg
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/sounds/snd_swing.ogg assets/sounds/snd_swing.ogg
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/hearth1.png assets/ui/hearth1.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/hearth2.png assets/ui/hearth2.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/hearth3_cancel.png assets/ui/hearth3_cancel.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/English.ini assets/ui/Lang/English.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/Italian.ini assets/ui/Lang/Italian.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/Portuguese-BR.ini assets/ui/Lang/Portuguese-BR.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/Spanish.ini assets/ui/Lang/Spanish.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/Turkish.ini assets/ui/Lang/Turkish.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/German.ini assets/ui/Lang/German.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/Russian.ini assets/ui/Lang/Russian.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/chapter0/data.win assets/chapter0/data.win
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/chapter0/options.ini assets/chapter0/options.ini
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/chapter0/dr.ini assets/chapter0/dr.ini
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_joybase_frame0.png assets/ui/spr_joybase_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_joystick_frame0.png assets/ui/spr_joystick_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_zkey_frame0.png assets/ui/spr_control_zkey_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_xkey_frame0.png assets/ui/spr_control_xkey_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/TouchControls/spr_control_ckey_frame0.png assets/ui/spr_control_ckey_frame0.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_joyL.png assets/ui/button_psv_joyL.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_joyR.png assets/ui/button_psv_joyR.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_L.png assets/ui/button_psv_L.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_R.png assets/ui/button_psv_R.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_select.png assets/ui/button_psv_select.png
    ${CMAKE_SOURCE_DIR}/../../data/assets/PSVitaButtons/button_psv_touchA.png assets/ui/button_psv_touchA.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_circle_0.png assets/ui/button_ps4_circle_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_cross_0.png assets/ui/button_ps4_cross_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_triangle_0.png assets/ui/button_ps4_triangle_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_square_0.png assets/ui/button_ps4_square_0.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_up.png assets/ui/button_ps4_dpad_up.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_down.png assets/ui/button_ps4_dpad_down.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_left.png assets/ui/button_ps4_dpad_left.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/button_ps4_dpad_right.png assets/ui/button_ps4_dpad_right.png
    ${CMAKE_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/buttons/spr_textbox_topleft_0.png assets/ui/spr_textbox_topleft_0.png
    ${LANGUAGE_ICON_VPK_FILES}
)

add_dependencies(Deltarune.vpk-vpk vitagl.bin-self)



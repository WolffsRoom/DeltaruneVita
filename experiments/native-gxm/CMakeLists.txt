cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK nao definido")
endif()
if(NOT DEFINED NANOVG_GXM_SOURCE_DIR OR NOT EXISTS "${NANOVG_GXM_SOURCE_DIR}/src/nanovg_gxm.h")
  message(FATAL_ERROR "Use -DNANOVG_GXM_SOURCE_DIR=<checkout fixado de xfangfang/nanovg-gxm>")
endif()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
project(deltarune_native_gxm_probe C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -ffast-math -mtune=cortex-a9 -mfpu=neon -Wall -Wextra")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(PORT_BUILD_VERSION "dev" CACHE STRING "Shared Deltarune Vita build label")
option(GXM_UNIFIED_BUILD "Build gxm.bin for the unified Deltarune VPK" OFF)

# nanovg-gxm talks to sceGxm directly.  Runtime shader compilation remains
# disabled: the probe uses its embedded GXP programs and therefore does not
# depend on VitaGL, vitashark or libshacccg.
set(USE_VITA_SHARK OFF CACHE BOOL "" FORCE)
add_subdirectory("${NANOVG_GXM_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/nanovg-gxm")

option(GXM_DIAGNOSTIC "Build standalone non-gameplay six-chapter diagnostic VPK" OFF)
if(GXM_DIAGNOSTIC)
  set(GXM_ENTRYPOINT diagnostic_main.c)
else()
  set(GXM_ENTRYPOINT main.c)
endif()

add_executable(${PROJECT_NAME}
  ${GXM_ENTRYPOINT} bs_bootstrap_probe.c bs_stb_ds.c bs_renderer_contract.c
  vita_settings_native.c native_settings_compat.c
  ../../src/vita-common/vita_backend_selection.c
  ../../src/vita-probe/source/vita_border_rules.c
  ../../src/vita-probe/source/vita_gpu_rules.c
  ../../src/vita-probe/source/vita_trophies.c
  ../../src/butterscotch/src/data_win.c
  ../../src/butterscotch/src/binary_reader.c
  ../../src/butterscotch/src/image/image_decoder.c
  ../../src/butterscotch/src/audio/openal/al_audio_system.c
  ../../src/butterscotch/src/audio/openal/wave.c
  ../../src/butterscotch/vendor/bzip2/bz_internal_error.c
  ../../src/butterscotch/vendor/bzip2/bzlib.c
  ../../src/butterscotch/vendor/bzip2/crctable.c
  ../../src/butterscotch/vendor/bzip2/decompress.c
  ../../src/butterscotch/vendor/bzip2/huffman.c
  ../../src/butterscotch/vendor/bzip2/randtable.c
)
target_include_directories(${PROJECT_NAME} PRIVATE
  ../../src/butterscotch/src
  ../../src/vita-probe/source
  ../../src/butterscotch/src/image
  ../../src/butterscotch/src/image
  ../../src/butterscotch/vendor/stb/image
  ../../src/butterscotch/vendor/stb/ds
  ../../src/butterscotch/vendor/bzip2
  ../../src/butterscotch/src/audio/openal
  ../../src/butterscotch/vendor/stb/vorbis
)
# Compile the original Butterscotch VM and its shared runtime dependencies.
# Dead section elimination is necessary while the renderer is not wired.
file(GLOB BS_VM_CORE_SOURCES "../../src/butterscotch/src/*.c")
list(REMOVE_ITEM BS_VM_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/butterscotch/src/data_win.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/butterscotch/src/binary_reader.c")
target_sources(${PROJECT_NAME} PRIVATE ${BS_VM_CORE_SOURCES}
  ../../src/butterscotch/vendor/md5/md5.c
  ../../src/butterscotch/vendor/sha1/sha1.c
  ../../src/butterscotch/vendor/base64/base64.c)
target_include_directories(${PROJECT_NAME} PRIVATE
  ../../src/butterscotch/vendor/md5
  ../../src/butterscotch/vendor/sha1
  ../../src/butterscotch/vendor/base64)
target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_WAD17)
if(NOT GXM_DIAGNOSTIC)
  target_compile_definitions(${PROJECT_NAME} PRIVATE NATIVE_GXM_VIDEO)
  target_sources(${PROJECT_NAME} PRIVATE native_video_gxm.c)
endif()
# Only OpenAL uses existing Vita-specific diagnostics/streaming. Keep the
# standalone Runner and GXM renderer independent of legacy VitaGL hooks.
set_source_files_properties(../../src/butterscotch/src/audio/openal/al_audio_system.c
  PROPERTIES COMPILE_DEFINITIONS PLATFORM_VITA)
set_source_files_properties(../../src/vita-probe/source/vita_trophies.c
  PROPERTIES COMPILE_DEFINITIONS VITA_TROPHIES_HOST_PRESENT)

target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wno-unused-parameter)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)

target_include_directories(${PROJECT_NAME} PRIVATE "${NANOVG_GXM_SOURCE_DIR}/src")
target_compile_definitions(${PROJECT_NAME} PRIVATE
  NATIVE_GXM_PROBE_VERSION="${PORT_BUILD_VERSION}")
target_link_libraries(${PROJECT_NAME}
  nanovg
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceGxm_stub
  vitashark
  SceShaccCgExt
  SceShaccCg_stub
  taihen_stub
  SceAvPlayer_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceKernelThreadMgr_stub
  SceSysmem_stub
  openal
  pthread
  SceAudio_stub
  SceAudioIn_stub
  SceAppUtil_stub
  SceRtc_stub
  SceNpTrophy_stub
  m
)

# Package the EXACT control-icon and translation resources used by the
# playable VitaSettings source, instead of drawing text-only placeholders.
set(GXM_SETTINGS_UI_FILES)
foreach(UI_GROUP
  "../../data/prepared/deltarune/deltarunevita/ui/buttons/*.png"
  "../../data/assets/PSVitaButtons/*.png"
  "../../data/assets/TouchControls/*.png"
  "../../data/assets/Trophies/*.png")
  file(GLOB UI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${UI_GROUP}")
  foreach(UI_FILE ${UI_FILES})
    get_filename_component(UI_NAME "${UI_FILE}" NAME)
    list(APPEND GXM_SETTINGS_UI_FILES "${UI_FILE}" "assets/ui/${UI_NAME}")
  endforeach()
endforeach()
file(GLOB GXM_SETTINGS_ICON_LANG "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/Lang/icons/*.png")
foreach(UI_FILE ${GXM_SETTINGS_ICON_LANG})
  get_filename_component(UI_NAME "${UI_FILE}" NAME)
  list(APPEND GXM_SETTINGS_UI_FILES "${UI_FILE}" "assets/ui/Lang/${UI_NAME}")
endforeach()
file(GLOB GXM_SETTINGS_LANG "${CMAKE_CURRENT_SOURCE_DIR}/../../data/prepared/deltarune/deltarunevita/ui/Lang/*.ini")
foreach(UI_FILE ${GXM_SETTINGS_LANG})
  get_filename_component(UI_NAME "${UI_FILE}" NAME)
  list(APPEND GXM_SETTINGS_UI_FILES "${UI_FILE}" "assets/ui/Lang/${UI_NAME}")
endforeach()

vita_create_self(gxm.bin ${PROJECT_NAME} UNSAFE)
if(NOT GXM_UNIFIED_BUILD)
  if(GXM_DIAGNOSTIC)
    vita_create_vpk(Deltarune-GXM-Audit-v0.78-a3.vpk DLTGMAUD3 gxm.bin
      VERSION 00.78
      NAME "Deltarune GXM Audit"
    )
  else()
    vita_create_vpk(Deltarune-NativeGXM-${PORT_BUILD_VERSION}.vpk DLTAGXM48 gxm.bin
      VERSION 00.78
      NAME "Deltarune Vita - Native GXM"
      FILE ${GXM_SETTINGS_UI_FILES}
    )
  endif()
endif()


cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK nao definido")
endif()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
project(deltarune_butterscotch_probe C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfloat-abi=softfp")

option(PLAYABLE_RUNNER "Build full Butterscotch Vita runner" ON)
option(VITAGL_PROBE "Build isolated vitaGL diagnostic" OFF)
if(PLAYABLE_RUNNER)
  file(GLOB BS_COMMON_SOURCES ../butterscotch/src/*.c)
  file(GLOB BS_GL_SOURCES
    ../butterscotch/src/gl_common/*.c
    ../butterscotch/src/gl_legacy/*.c
    ../butterscotch/src/image/*.c
  )
  add_executable(${PROJECT_NAME}
    source/playable_main.c
    source/vita_settings.c
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
  ../butterscotch/src
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
  ENABLE_WAD17 ENABLE_LEGACY_GL PLATFORM_VITA
  BUTTERSCOTCH_COMMIT_HASH="vita-port"
  BUTTERSCOTCH_COMMIT_DATE="2026-07-15"
)
target_link_libraries(${PROJECT_NAME}
  vitaGL
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
  SceSysmem_stub
  SceTouch_stub
  kubridge_stub
  mathneon
  openal
  pthread
  SceAudio_stub
  SceAudioIn_stub
  z
  m
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(Deltarune.vpk DLTVITA01 eboot.bin
  VERSION 00.34
  NAME "Deltarune"
  FILE
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/icon0.png sce_sys/icon0.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/pic0.png sce_sys/pic0.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/001.png sce_sys/manual/001.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/002.png sce_sys/manual/002.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/003.png sce_sys/manual/003.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/004.png sce_sys/manual/004.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/005.png sce_sys/manual/005.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/006.png sce_sys/manual/006.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/007.png sce_sys/manual/007.png
    ${CMAKE_SOURCE_DIR}/../../data/legacy-assets/manual/008.png sce_sys/manual/008.png
)

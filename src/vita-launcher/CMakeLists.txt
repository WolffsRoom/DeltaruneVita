add_executable(deltarune_renderer_launcher
    main.c
    launcher_ui.c
    ../vita-common/vita_backend_selection.c
    $ENV{VITASDK}/share/gcc-arm-vita-eabi/samples/common/debugScreen.c
)
target_include_directories(deltarune_renderer_launcher PRIVATE
    .
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

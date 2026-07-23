/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/diagnostic.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define CPP_LOAD_ADDRESS 0x98000000
#define LOAD_ADDRESS     0x99000000

extern so_module so_mod;
extern so_module cpp_mod;

void soloader_init_all() {
	diagnostic_phase("INIT=apputil_begin");
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    diagnostic_phase("INIT=clocks_complete");

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif
    diagnostic_phase("INIT=fios_complete");

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");
    diagnostic_phase("INIT=kubridge_complete");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }

    if (!file_exists(CPP_SO_PATH)) {
        fatal_error("Missing required Android C++ runtime: %s.", CPP_SO_PATH);
    }

    if (!file_exists(APK_PATH)) {
        fatal_error("Missing the supported user-supplied APK: %s.", APK_PATH);
    }

    if (so_file_load(&cpp_mod, CPP_SO_PATH, CPP_LOAD_ADDRESS) < 0) {
        l_fatal("Android libc++ could not be loaded.");
        fatal_error("Error: could not load %s.", CPP_SO_PATH);
    }
    l_success("Android libc++ loaded.");
    diagnostic_phase("INIT=cpp_load_complete");

    diagnostic_phase("INIT=runner_load_begin");
    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }
    diagnostic_phase("INIT=runner_load_complete");

    settings_load();
    l_success("Settings loaded.");
    diagnostic_phase("INIT=settings_complete");

    diagnostic_phase("INIT=cpp_relocate_begin");
    so_relocate(&cpp_mod);
    diagnostic_phase("INIT=cpp_resolve_begin");
    resolve_imports(&cpp_mod);
    diagnostic_phase("INIT=cpp_initialize_begin");
    so_flush_caches(&cpp_mod);
    so_initialize(&cpp_mod);
    l_success("Android libc++ initialized.");
    diagnostic_phase("INIT=cpp_initialize_complete");

    diagnostic_phase("INIT=runner_relocate_begin");
    so_relocate(&so_mod);
    l_success("SO relocated.");

    diagnostic_phase("INIT=runner_resolve_begin");
    resolve_imports(&so_mod);
    l_success("SO imports resolved.");
    diagnostic_phase("INIT=runner_resolve_complete");

    diagnostic_phase("INIT=patch_begin");
    so_patch();
    l_success("SO patched.");

    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    so_initialize(&so_mod);
    l_success("SO initialized.");
    diagnostic_phase("INIT=runner_initialize_complete");

    diagnostic_phase("INIT=gl_preload_begin");
    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");
    diagnostic_phase("INIT=jni_complete");

    controls_init();
    l_success("Controls initialized.");
    diagnostic_phase("INIT=controls_complete");
}

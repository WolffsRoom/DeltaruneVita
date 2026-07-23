#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/diagnostic.h"
#include "utils/logger.h"

#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;
so_module cpp_mod;

static JNIEnv runner_env;
static int (*runner_key_event)(JNIEnv, int, int, int, int, int);
static int (*runner_touch_event)(JNIEnv, int, int, int, float, float);

int main() {
    log_reset();
    log_marker("BUILD_ID=" DELTARUNE_BUILD_ID);
    log_marker("RUNNER_SHA256=" DELTARUNE_RUNNER_SHA256);
    log_marker("CPP_SHA256=" DELTARUNE_CPP_SHA256);
    diagnostic_start();
    diagnostic_phase("PHASE=main_enter");

    soloader_init_all();
    diagnostic_phase("PHASE=soloader_init_complete");

    int (* JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (JNI_OnLoad) {
        diagnostic_phase("PHASE=jni_onload_begin");
        JNI_OnLoad(&jvm);
        diagnostic_phase("PHASE=jni_onload_complete");
    } else {
        log_marker("JNI_OnLoad=not_exported");
    }

    gl_init();
    diagnostic_phase("PHASE=gl_init_complete");

    void (*runner_startup)(JNIEnv, int, const char *, const char *, const char *, int) =
        (void *)so_symbol(&so_mod,
            "Java_com_yoyogames_runner_RunnerJNILib_Startup");
    if (!runner_startup) {
        log_marker("FATAL=RunnerJNILib_Startup_not_exported");
        sceKernelExitDeleteThread(-1);
    }

    diagnostic_phase("PHASE=runner_startup_begin");
    runner_startup(jni, 0, APK_PATH, DATA_PATH, "com.hadrian.deltarune", 0);
    diagnostic_phase("PHASE=runner_startup_complete");

    runner_env = jni;
    runner_key_event = (void *)so_symbol(&so_mod,
        "Java_com_yoyogames_runner_RunnerJNILib_KeyEvent");
    runner_touch_event = (void *)so_symbol(&so_mod,
        "Java_com_yoyogames_runner_RunnerJNILib_TouchEvent");
    int (*runner_process)(JNIEnv, int, int, int, float, float, float, int, int, float) =
        (void *)so_symbol(&so_mod,
            "Java_com_yoyogames_runner_RunnerJNILib_Process");
    if (!runner_process) {
        log_marker("FATAL=RunnerJNILib_Process_not_exported");
        sceKernelExitDeleteThread(-1);
    }

    diagnostic_phase("PHASE=main_loop_enter");
    uint32_t frame = 0;
    while (1) {
        frame++;
        if (frame <= 10 || frame % 300 == 0)
            diagnostic_frame(frame, "FRAME=before_controls");
        controls_poll();
        if (frame <= 10 || frame % 300 == 0)
            diagnostic_frame(frame, "FRAME=before_runner_process");
        runner_process(runner_env, 0, 960, 544, 0.0f, 0.0f, 0.0f,
                       0, 0, 60.0f);
        if (frame <= 10 || frame % 300 == 0)
            diagnostic_frame(frame, "FRAME=after_runner_process");
        gl_swap();
        if (frame <= 10 || frame % 300 == 0)
            diagnostic_frame(frame, "FRAME=after_gl_swap");
    }

    sceKernelExitDeleteThread(0);
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (runner_key_event && action != CONTROLS_ACTION_MOVE) {
        int state = action == CONTROLS_ACTION_UP ? 1 : 0;
        runner_key_event(runner_env, 0, state, keycode, keycode, 0x101);
    }
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    if (runner_touch_event) {
        int type = action == CONTROLS_ACTION_DOWN ? 0 :
                   action == CONTROLS_ACTION_UP ? 1 : 2;
        runner_touch_event(runner_env, 0, type, 0, x, y);
    }
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    // Call into the .so here
}

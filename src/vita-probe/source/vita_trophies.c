#include "vita_trophies.h"

#include <psp2/appmgr.h>
#include <psp2/common_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <string.h>
#include <stdio.h>
#include <vitaGL.h>

// --- Diagnostic trophy logging (v0.70-36) ------------------------------------
// Writes directly to the probe log with open/append/close (immediate flush), so
// the last line survives even if the app is killed by a system error dialog
// right after. Unconditional on purpose for this diagnostic build.
static void trophyLogMsg(const char* text) {
    if (text == NULL) return;
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}
static void trophyLogStage(const char* stage, int result) {
    char buf[192];
    snprintf(buf, sizeof(buf), "TROPHY=%s result=0x%08X", stage, (unsigned int)result);
    trophyLogMsg(buf);
}

typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    int context;
    int options;
    unsigned char reserved[128];
} SceNpTrophySetupDialogParam;

typedef struct { unsigned int bits[4]; } SceNpTrophyUnlockState;

int sceNpTrophyInit(void* unk);
int sceNpTrophyCreateContext(int* context, char* commId, char* commSign, unsigned long long options);
int sceNpTrophySetupDialogInit(SceNpTrophySetupDialogParam* param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
int sceNpTrophySetupDialogTerm(void);
int sceNpTrophyCreateHandle(int* handle);
int sceNpTrophyDestroyHandle(int handle);
int sceNpTrophyUnlockTrophy(int context, int handle, int id, int* platinumId);
int sceNpTrophyGetTrophyUnlockState(int context, int handle, SceNpTrophyUnlockState* state, unsigned int* count);

typedef struct { int result; char reserved[128]; } SceNpTrophySetupDialogResultDiag;
int sceNpTrophySetupDialogGetResult(SceNpTrophySetupDialogResultDiag* result);

// vitaGL internal flag: vglSwapBuffers(GL_TRUE) only drives sceCommonDialogUpdate
// when this is GL_TRUE (set GL_FALSE only when a dedicated cdialog pool is
// reserved via cdlg_pool_size>0 in vglInit). Logged for diagnosis.
extern GLboolean vgl_has_cdlg_support;
extern void VitaProbe_pumpLoadingScreen(void);

static int trophyContext = -1;
static SceNpTrophyUnlockState unlockState;
static SceNpTrophyUnlockState pendingState;
static bool available = false;
static SceUID unlockSemaphore = -1;
static SceUID unlockThread = -1;
static volatile unsigned int queueRead = 0;
static volatile unsigned int queueWrite = 0;
static int unlockQueue[32];
static const char* lastStage = "not_started";
static int lastResult = 0;

static bool trophyFail(const char* stage, int result) {
    lastStage = stage;
    lastResult = result;
    trophyLogStage(stage, result);
    trophyLogMsg("TROPHY=FAILED_returning_false");
    return false;
}

const char* VitaTrophies_lastStage(void) { return lastStage; }
int VitaTrophies_lastResult(void) { return lastResult; }

static int trophyUnlockWorker(SceSize args, void* argp) {
    (void)args;
    (void)argp;
    for (;;) {
        if (sceKernelWaitSema(unlockSemaphore, 1, NULL) < 0) break;
        if (!available) break;
        unsigned int read = queueRead;
        __sync_synchronize();
        int id = unlockQueue[read & 31U];
        queueRead = read + 1U;

        int handle = -1, platinumId = -1;
        unsigned int mask = 1U << (id & 31);
        int handleResult = sceNpTrophyCreateHandle(&handle);
        if (handleResult >= 0) {
            int result = sceNpTrophyUnlockTrophy(trophyContext, handle, id, &platinumId);
            sceNpTrophyDestroyHandle(handle);
            if (result >= 0) {
                __sync_fetch_and_or(&unlockState.bits[id >> 5], mask);
                // The NP service can award platinum together with the final
                // regular trophy. Preserve that returned state instead of
                // waiting for another unlock request.
                if (platinumId >= 0 && platinumId < 30) {
                    unsigned int platinumMask = 1U << (platinumId & 31);
                    __sync_fetch_and_or(&unlockState.bits[platinumId >> 5], platinumMask);
                }
            }
            char detail[160];
            snprintf(detail, sizeof(detail),
                     "TROPHY=unlock id=%d result=0x%08X platinum=%d",
                     id, (unsigned int)result, platinumId);
            trophyLogMsg(detail);
        } else {
            trophyLogStage("unlock_create_handle", handleResult);
        }
        __sync_fetch_and_and(&pendingState.bits[id >> 5], ~mask);
    }
    return 0;
}

bool VitaTrophies_init(void) {
    // The chapter runner can revisit this initialization path without ending
    // the process. Keep trophy setup idempotent and preserve the live context.
    if (available) {
        trophyLogMsg("TROPHY=init_already_ready");
        lastStage = "ready";
        lastResult = 0;
        return true;
    }
    trophyLogMsg("TROPHY=init_start");
    lastStage = "pack_open";
    lastResult = 0;
    trophyLogMsg("TROPHY=pack_open_before");
    SceUID pack = sceIoOpen("app0:sce_sys/trophy/DELT00001_01/TROPHY.TRP", SCE_O_RDONLY, 0);
    trophyLogStage("pack_open_after", pack);
    if (pack < 0) return trophyFail("pack_missing", pack);
    sceIoClose(pack);

    // NP communication IDs are binary 12-byte records (9-byte base ID,
    // underscore and two digits), not ordinary NUL-terminated strings.
    char communicationIdText[16] = {0};
    unsigned char communicationId[12] = {0};
    // sceAppMgr exposes the SceNpCommunicationId structure, not the textual
    // SFO spelling. "DELT00001_00" therefore arrives as the nine-byte ID,
    // a NUL terminator, set number 0 and one reserved byte.
    // 12-byte SceNpCommunicationId: 9-byte base "DELT00001", NUL terminator,
    // set number, reserved. Set number is 1 (=> DELT00001_01).
    static const unsigned char expectedId[12] = {
        'D','E','L','T','0','0','0','0','1','\0',1,0
    };
    char signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};
    trophyLogMsg("TROPHY=app_param_before");
    int result = sceAppMgrAppParamGetString(0, 12, communicationIdText,
                                             sizeof(communicationIdText));
    trophyLogStage("app_param_after", result);  // diagnostic only now (SFO comm id is empty)
    {
        char idbuf[160];
        snprintf(idbuf, sizeof(idbuf),
                 "TROPHY=comm_id sfo_read='%.12s' bytes=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 communicationIdText,
                 (unsigned char)communicationIdText[0], (unsigned char)communicationIdText[1],
                 (unsigned char)communicationIdText[2], (unsigned char)communicationIdText[3],
                 (unsigned char)communicationIdText[4], (unsigned char)communicationIdText[5],
                 (unsigned char)communicationIdText[6], (unsigned char)communicationIdText[7],
                 (unsigned char)communicationIdText[8], (unsigned char)communicationIdText[9],
                 (unsigned char)communicationIdText[10], (unsigned char)communicationIdText[11]);
        trophyLogMsg(idbuf);
    }
    // param.sfo no longer declares NP_COMMUNICATION_ID (see CMakeLists) so the
    // system does not run app-level NP validation that rejected the set with
    // C0-11133-9. The SFO read above is only for diagnostics; use the hardcoded
    // comm id directly for the context, exactly like the working Animal Crossing
    // (ACGC00001_01) and Cuphead (CUPH44444_00) ports. Do NOT abort on an empty
    // SFO comm id.
    memcpy(communicationId, expectedId, sizeof(communicationId));
    trophyLogMsg("TROPHY=comm_id_source=hardcoded value=DELT00001_01");
    trophyLogMsg("TROPHY=load_np_trophy_before");
    result = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    trophyLogStage("load_np_trophy_after", result);
    if (result < 0) return trophyFail("load_np_trophy", result);
    trophyLogMsg("TROPHY=np_trophy_init_before");
    result = sceNpTrophyInit(NULL);
    trophyLogStage("np_trophy_init_after", result);
    // 0x80551602 means the process already initialized NP Trophy. It is not a
    // fatal error and context creation remains valid. This also protects
    // against future callers initializing the service before this subsystem.
    if (result < 0 && result != (int)0x80551602)
        return trophyFail("np_trophy_init", result);
    if (result == (int)0x80551602)
        trophyLogMsg("TROPHY=np_trophy_already_initialized_continue");
    trophyLogMsg("TROPHY=create_context_before");
    result = sceNpTrophyCreateContext(&trophyContext, (char*)communicationId,
                                       signature, 0);
    trophyLogStage("create_context_after", result);
    if (result < 0)
        return trophyFail(result == (int)0x8055160C ?
                          "create_context_signature_no_notrpdrm" : "create_context",
                          result);
    trophyLogMsg("TROPHY=create_context_ok");

    // Registration on PS Vita is ONLY available through the system setup dialog
    // (sceNpTrophyRegisterContext does not exist on Vita). The dialog previously
    // hung forever in status RUNNING (permanent black screen) because the
    // common-dialog subsystem was never configured on the normal boot path and
    // frames were not cleared. Mirror the working message-dialog path from
    // playable_main.c: sceAppUtilInit + sceCommonDialogSetConfigParam, then
    // glClear + vglSwapBuffers(GL_TRUE) each iteration.
    SceAppUtilInitParam appUtilParam;
    SceAppUtilBootParam appUtilBootParam;
    memset(&appUtilParam, 0, sizeof(appUtilParam));
    memset(&appUtilBootParam, 0, sizeof(appUtilBootParam));
    int appUtilRes = sceAppUtilInit(&appUtilParam, &appUtilBootParam);
    trophyLogStage("app_util_init", appUtilRes);

    // sceCommonDialogConfigParamInit leaves language/enterButtonAssign as an
    // "unset" sentinel, so SetConfigParam rejects it with 0x80020431
    // (SCE_COMMON_DIALOG_ERROR_INVALID_LANGUAGE) and the trophy dialog can never
    // render (stuck RUNNING / black screen). Fill both from the system params.
    SceCommonDialogConfigParam dialogConfig;
    sceCommonDialogConfigParamInit(&dialogConfig);
    int sysLang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    int enterBtn = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &sysLang);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enterBtn);
    dialogConfig.language = (SceSystemParamLang)sysLang;
    dialogConfig.enterButtonAssign = (SceSystemParamEnterButtonAssign)enterBtn;
    int cfgRes = sceCommonDialogSetConfigParam(&dialogConfig);
    trophyLogStage("common_dialog_config", cfgRes);
    {
        char lbuf[128];
        snprintf(lbuf, sizeof(lbuf), "TROPHY=dialog_cfg lang=%d enter=%d cdlg_support=%d",
                 sysLang, enterBtn, (int)vgl_has_cdlg_support);
        trophyLogMsg(lbuf);
    }

    // Present warm-up frames so the display/GXM back buffer is established before
    // the common dialog runs. Without a settled display the dialog can stay in
    // RUNNING forever (nothing valid to composite onto / present).
    for (int w = 0; w < 8; ++w) {
        VitaProbe_pumpLoadingScreen();
        vglSwapBuffers(GL_FALSE);
    }
    trophyLogMsg("TROPHY=warmup_frames_done");

    SceNpTrophySetupDialogParam setup;
    memset(&setup, 0, sizeof(setup));
    _sceCommonDialogSetMagicNumber(&setup.commonParam);
    setup.sdkVersion = PSP2_SDK_VERSION;
    setup.context = trophyContext;
    trophyLogMsg("TROPHY=setup_dialog_init_before");
    result = sceNpTrophySetupDialogInit(&setup);
    trophyLogStage("setup_dialog_init_after", result);
    if (result < 0) { sceAppUtilShutdown(); return trophyFail("setup_dialog_init", result); }

    trophyLogMsg("TROPHY=dialog_loop_enter");
    {
        int guard = 0;
        SceCommonDialogStatus st = SCE_COMMON_DIALOG_STATUS_RUNNING;
        while ((st = sceNpTrophySetupDialogGetStatus()) == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            if ((guard % 60) == 0) trophyLogStage("dialog_loop_running status", (int)st);
            // Keep the chapter Loading frame behind the native trophy setup
            // dialog. Previously this loop cleared both buffers to black for
            // up to a minute before data.win parsing even began.
            VitaProbe_pumpLoadingScreen();
            vglSwapBuffers(GL_TRUE);
            if (++guard > 3600) {  // ~60-120s cap so a stuck dialog logs a clear TIMEOUT
                trophyLogMsg("TROPHY=dialog_loop_TIMEOUT_breaking");
                break;
            }
        }
        trophyLogStage("dialog_loop_exit status", (int)st);
    }
    {
        SceNpTrophySetupDialogResultDiag dres;
        memset(&dres, 0, sizeof(dres));
        int gr = sceNpTrophySetupDialogGetResult(&dres);
        char rbuf[160];
        snprintf(rbuf, sizeof(rbuf),
                 "TROPHY=setup_dialog_result getresult=0x%08X dialog_result=0x%08X",
                 (unsigned int)gr, (unsigned int)dres.result);
        trophyLogMsg(rbuf);
    }
    sceNpTrophySetupDialogTerm();
    sceAppUtilShutdown();
    trophyLogMsg("TROPHY=setup_dialog_term_done");

    int handle = -1;
    unsigned int count = 0;
    trophyLogMsg("TROPHY=create_handle_before");
    int hres = sceNpTrophyCreateHandle(&handle);
    trophyLogStage("create_handle_after", hres);
    if (hres >= 0) {
        int gus = sceNpTrophyGetTrophyUnlockState(trophyContext, handle, &unlockState, &count);
        trophyLogStage("get_unlock_state", gus);
        sceNpTrophyDestroyHandle(handle);
    }
    memset(&pendingState, 0, sizeof(pendingState));
    unlockSemaphore = sceKernelCreateSema("deltarune trophy queue", 0, 0, 32, NULL);
    if (unlockSemaphore < 0) return trophyFail("create_semaphore", unlockSemaphore);
    unlockThread = sceKernelCreateThread("deltarune trophy unlocker", trophyUnlockWorker,
                                         0x10000100, 0x10000, 0, 0, NULL);
    available = true;
    if (unlockThread < 0 || sceKernelStartThread(unlockThread, 0, NULL) < 0) {
        available = false;
        return trophyFail("start_unlock_thread", unlockThread);
    }
    lastStage = "ready";
    lastResult = 0;
    trophyLogMsg("TROPHY=ready_returning_true");
    return true;
}

void VitaTrophies_unlock(int id) {
    // ID 0 is platinum and must only be awarded by NP Trophy after the final linked trophy.
    if (!available || id <= 0 || id >= 30) return;
    unsigned int mask = 1U << (id & 31);
    if (((unlockState.bits[id >> 5] | pendingState.bits[id >> 5]) & mask) != 0) return;
    unsigned int write = queueWrite;
    if (write - queueRead >= 32U) return;
    __sync_fetch_and_or(&pendingState.bits[id >> 5], mask);
    unlockQueue[write & 31U] = id;
    __sync_synchronize();
    queueWrite = write + 1U;
    sceKernelSignalSema(unlockSemaphore, 1);
}

unsigned int VitaTrophies_syncMask(uint32_t unlockedMask) {
    if (!available) return 0;
    unsigned int queued = 0;
    for (int id = 1; id < 30; ++id) {
        unsigned int mask = 1U << (id & 31);
        if ((unlockedMask & (1U << id)) == 0 ||
            ((unlockState.bits[id >> 5] | pendingState.bits[id >> 5]) & mask) != 0)
            continue;
        VitaTrophies_unlock(id);
        ++queued;
    }
    return queued;
}

void VitaTrophies_shutdown(void) {
    available = false;
    if (unlockThread >= 0) {
        sceKernelSignalSema(unlockSemaphore, 1);
        sceKernelWaitThreadEnd(unlockThread, NULL, NULL);
        sceKernelDeleteThread(unlockThread);
        unlockThread = -1;
    }
    if (unlockSemaphore >= 0) {
        sceKernelDeleteSema(unlockSemaphore);
        unlockSemaphore = -1;
    }
    trophyContext = -1;
}

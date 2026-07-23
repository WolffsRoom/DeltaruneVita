/*
 * FalsoJNI_Logger.c
 *
 * Fake Java Native Interface, providing JavaVM and JNIEnv objects.
 *
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdarg.h>
#include <pthread.h>
#include <malloc.h>
#include <string.h>

#include "FalsoJNI_Logger.h"
#include "FalsoJNI.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#define COLOR_RED     "\x1B[31m"
#define COLOR_ORANGE  "\x1B[33m"
#define COLOR_BLUE    "\x1B[34m"

#define COLOR_END     "\033[0m"

static SceKernelLwMutexWork _fjni_log_mutex;
static volatile short int _fjni_log_mutex_inited = 0;

static char _fjni_log_buffer_1[2048];
static char _fjni_log_buffer_2[2048];

#define LOG_LOCK \
    if (!_fjni_log_mutex_inited) { \
        int ret = sceKernelCreateLwMutex(&_fjni_log_mutex, "fjni_log_lock", 0, 0, NULL); \
        if (ret < 0) { \
            sceClibPrintf("[JNI] Error: failed to create log mutex: 0x%x\n", ret); \
            return; \
        } \
        _fjni_log_mutex_inited = 1; \
    } \
    sceKernelLockLwMutex(&_fjni_log_mutex, 1, NULL);

#define LOG_UNLOCK \
    if (_fjni_log_mutex_inited) { \
        sceKernelUnlockLwMutex(&_fjni_log_mutex, 1); \
    }

#define LOG_PRINT \
    va_list list; \
    va_start(list, fmt); \
    sceClibVsnprintf(_fjni_log_buffer_2, sizeof(_fjni_log_buffer_2) - 1, _fjni_log_buffer_1, list); \
    va_end(list); \
    sceClibPrintf(_fjni_log_buffer_2);

void _fjni_log_info(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_INFO
    LOG_LOCK

    sceClibSnprintf(_fjni_log_buffer_1, sizeof(_fjni_log_buffer_1) - 1,
                    "%s[INFO] %s%s\n", COLOR_BLUE, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _fjni_log_warn(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_WARN
    LOG_LOCK

    sceClibSnprintf(_fjni_log_buffer_1, sizeof(_fjni_log_buffer_1) - 1,
                    "%s[WARN][%s:%d][%s] %s%s\n", COLOR_ORANGE, fi, li, fn, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _fjni_log_debug(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_ALL
    LOG_LOCK

    sceClibSnprintf(_fjni_log_buffer_1, sizeof(_fjni_log_buffer_1) - 1,
                    "[DBG][%s:%d][%s] %s\n", fi, li, fn, fmt);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _fjni_log_error(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_ERROR
    LOG_LOCK

    sceClibSnprintf(_fjni_log_buffer_1, sizeof(_fjni_log_buffer_1) - 1,
                    "%s[ERROR][%s:%d][%s] %s%s\n", COLOR_RED, fi, li, fn, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

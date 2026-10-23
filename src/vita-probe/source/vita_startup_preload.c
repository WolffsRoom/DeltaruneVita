#include "vita_startup_preload.h"
#include "vita_io_profile.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StartupParseJob {
    char path[256];
    DataWinParserOptions options;
    uint64_t parseBeginUs;
    uint64_t parseEndUs;
} StartupParseJob;

static bool startupParseJob(void* userData, void** result,
                            size_t* resultBytes) {
    StartupParseJob* job = userData;
    if (job == NULL || result == NULL || resultBytes == NULL) return false;
    job->parseBeginUs = sceKernelGetProcessTimeWide();
    VitaIoProfileSample io = VitaIoProfile_begin(
        "chapter0_parse", "data_win", 0, 0, true);
    DataWin* parsed = DataWin_parse(job->path, job->options);
    job->parseEndUs = sceKernelGetProcessTimeWide();
    VitaIoProfile_end(&io, parsed != NULL,
                      job->parseEndUs - job->parseBeginUs, 0);
    char ioLine[256];
    VitaIoProfile_format(&io, ioLine, sizeof(ioLine));
    fprintf(stderr, "%s\n", ioLine);
    *result = parsed;
    *resultBytes = parsed != NULL ? sizeof(*parsed) : 0;
    return parsed != NULL;
}

bool VitaStartupPreload_begin(VitaStartupPreload* preload,
                              VitaBackgroundWork* work,
                              const char* path,
                              DataWinParserOptions options,
                              uint32_t generation) {
    if (preload == NULL || work == NULL || path == NULL || path[0] == '\0')
        return false;
    memset(preload, 0, sizeof(*preload));
    preload->work = work;
    preload->generation = generation;
    preload->stats.beginUs = sceKernelGetProcessTimeWide();
    preload->stats.usedUnlockedCore =
        VitaBackgroundWork_usedUnlockedCore(work);
    snprintf(preload->path, sizeof(preload->path), "%s", path);

    StartupParseJob* parse = calloc(1, sizeof(*parse));
    if (parse == NULL) return false;
    snprintf(parse->path, sizeof(parse->path), "%s", path);
    parse->options = options;
    parse->options.progressCallback = NULL;

    VitaBackgroundJob job;
    memset(&job, 0, sizeof(job));
    job.generation = generation;
    job.reservedBytes = 0;
    job.execute = startupParseJob;
    job.destroyUserData = free;
    job.destroyResult = (VitaBackgroundDestroy)DataWin_free;
    job.userData = parse;
    if (!VitaBackgroundWork_submit(work, &job, &preload->ticket)) {
        free(parse);
        return false;
    }
    preload->stats.submitted = true;
    return true;
}

DataWin* VitaStartupPreload_join(VitaStartupPreload* preload,
                                 const char* expectedPath) {
    if (preload == NULL || !preload->stats.submitted || expectedPath == NULL)
        return NULL;
    bool pathMatches = strcmp(preload->path, expectedPath) == 0;
    if (!pathMatches)
        VitaBackgroundWork_cancelGeneration(preload->work,
                                            preload->generation);

    preload->stats.joinBeginUs = sceKernelGetProcessTimeWide();
    VitaBackgroundJob completed;
    for (;;) {
        if (VitaBackgroundWork_poll(preload->work, &completed)) break;
        sceKernelDelayThread(1000);
    }
    preload->stats.joinEndUs = sceKernelGetProcessTimeWide();
    if (completed.ticket != preload->ticket) {
        VitaBackgroundJob_release(&completed);
        return NULL;
    }
    StartupParseJob* parse = completed.userData;
    if (parse != NULL) {
        preload->stats.parseBeginUs = parse->parseBeginUs;
        preload->stats.parseEndUs = parse->parseEndUs;
    }
    DataWin* result = NULL;
    if (pathMatches && completed.state == VITA_BACKGROUND_JOB_READY) {
        result = completed.result;
        completed.result = NULL;
        preload->stats.adopted = true;
    }
    VitaBackgroundJob_release(&completed);
    preload->stats.submitted = false;
    return result;
}

void VitaStartupPreload_cancel(VitaStartupPreload* preload) {
    if (preload == NULL || !preload->stats.submitted) return;
    VitaBackgroundWork_cancelGeneration(preload->work, preload->generation);
}

#include "common_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void RunnerArgs_initDefaults(RunnerArgs* args) {
    if (!args) return;
    args->gamePath = ".";
    args->dataWinPath = NULL;
    args->windowWidth = 960;
    args->windowHeight = 544;
    args->vsync = true;
    args->fullscreen = false;
    args->showFps = false;
    args->debugMode = false;
    args->targetFps = 60;
    args->recordInputPath = NULL;
    args->replayInputPath = NULL;
}

void RunnerArgs_parseCommandLineArguments(int argc, char** argv, RunnerArgs* outArgs) {
    RunnerArgs_initDefaults(outArgs);
    if (!outArgs || argc <= 1 || !argv) return;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            outArgs->dataWinPath = argv[++i];
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            outArgs->gamePath = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            outArgs->windowWidth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            outArgs->windowHeight = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            outArgs->fullscreen = true;
        } else if (strcmp(argv[i], "--fps") == 0) {
            outArgs->showFps = true;
        } else if (strcmp(argv[i], "--debug") == 0) {
            outArgs->debugMode = true;
        } else if (argv[i][0] != '-') {
            outArgs->gamePath = argv[i];
        }
    }
}

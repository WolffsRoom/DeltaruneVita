#ifndef BUTTERSCOTCH_COMMON_ARGS_H
#define BUTTERSCOTCH_COMMON_ARGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RunnerArgs {
    const char* gamePath;
    const char* dataWinPath;
    int windowWidth;
    int windowHeight;
    bool vsync;
    bool fullscreen;
    bool showFps;
    bool debugMode;
    int targetFps;
    const char* recordInputPath;
    const char* replayInputPath;
} RunnerArgs;

void RunnerArgs_initDefaults(RunnerArgs* args);
void RunnerArgs_parseCommandLineArguments(int argc, char** argv, RunnerArgs* outArgs);

#ifdef __cplusplus
}
#endif

#endif // BUTTERSCOTCH_COMMON_ARGS_H

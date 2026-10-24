#ifndef DELTARUNE_BS_BOOTSTRAP_PROBE_H
#define DELTARUNE_BS_BOOTSTRAP_PROBE_H
typedef struct DataWin DataWin;
typedef struct VMContext VMContext;
typedef struct Runner Runner;
typedef struct Renderer Renderer;
typedef struct FileSystem FileSystem;
typedef struct AudioSystem AudioSystem;
typedef struct {
    DataWin *data;
    VMContext *vm;
    Runner *runner;
    Renderer *renderer;
    FileSystem *fs;
    AudioSystem *audio;
    char bundleRoot[192];
    char modRoot[192];
    int chapter;
    int runnerOk;
    int vmOk;
    unsigned vmFunctions;
    int vmBuiltinsRegistered;
    int parserOk;
    unsigned sprites, tpag, rooms, code, textures;
} BSBootstrapProbe;
int bs_bootstrap_load(BSBootstrapProbe *p, int chapter, const char *modName);
int bs_bootstrap_vm_create(BSBootstrapProbe *p);
int bs_bootstrap_runner_create(BSBootstrapProbe *p);
void bs_bootstrap_unload(BSBootstrapProbe *p);
#endif

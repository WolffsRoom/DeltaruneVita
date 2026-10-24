/*
 * Real Butterscotch parser bootstrap, isolated from the playable VitaGL app.
 * In this stage we initialize its DataWin representation, not the Runner VM.
 */
#include "bs_bootstrap_probe.h"
#include "data_win.h"
#include "vm.h"
#include "runner.h"
#include "audio/openal/al_audio_system.h"
#include "overlay_file_system.h"
#include <psp2/io/stat.h>
#include "bs_renderer_contract.h"
#include <stdio.h>
#include <string.h>

#define BS_CHAPTER_ROOT "ux0:data/deltarune/deltarunevita/"

int bs_bootstrap_load(BSBootstrapProbe *p, int chapter, const char *modName) {
    if (!p) return -1;
    if(chapter<0 || chapter>5)return -4;
    memset(p, 0, sizeof(*p));
    p->chapter=chapter;
    char dataWinPath[192];
    snprintf(p->bundleRoot,sizeof(p->bundleRoot),
             BS_CHAPTER_ROOT "chapter%d/",chapter);
    snprintf(dataWinPath,sizeof(dataWinPath),"%sdata.win",p->bundleRoot);
    SceIoStat st;
    /* Match VitaGL: translated data.win wins when present, while a language
     * directory without data.win still overlays loose assets. */
    if(modName && modName[0] && strcmp(modName,"English")!=0 &&
       strcmp(modName,"Original")!=0){
        const char *resolved=modName;
        snprintf(p->modRoot,sizeof(p->modRoot),
                 BS_CHAPTER_ROOT "mods/Lang/%s/chapter%d/",resolved,chapter);
        if(sceIoGetstat(p->modRoot,&st)<0 &&
           (!strcmp(modName,"Russkiy")||!strcmp(modName,"Russian"))){
            resolved=!strcmp(modName,"Russkiy")?"Russian":"Russkiy";
            snprintf(p->modRoot,sizeof(p->modRoot),
                     BS_CHAPTER_ROOT "mods/Lang/%s/chapter%d/",resolved,chapter);
        }
        char modData[192];
        snprintf(modData,sizeof(modData),"%sdata.win",p->modRoot);
        if(sceIoGetstat(modData,&st)>=0)
            snprintf(dataWinPath,sizeof(dataWinPath),"%s",modData);
    }
    if(sceIoGetstat(dataWinPath,&st)<0)return -5;
    DataWinParserOptions o = {0};
    o.parseGen8 = o.parseOptn = o.parseLang = true;
    o.parseExtn = o.parseSond = o.parseAgrp = true;
    o.parseSprt = o.parseBgnd = o.parsePath = true;
    o.parseScpt = o.parseGlob = o.parseShdr = true;
    o.parseFont = o.parseTmln = o.parseObjt = true;
    o.parseRoom = o.parseTpag = o.parseCode = true;
    o.parseVari = o.parseFunc = o.parseStrg = true;
    o.parseTxtr = o.parseAudo = true;
    o.skipLoadingPreciseMasksForNonPreciseSprites = true;
    o.lazyLoadRooms = true;
    o.lazyLoadTextures = true;
    o.lazyLoadAudio = true;
    o.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    p->data = DataWin_parse(dataWinPath, o);
    if (!p->data) return -2;
    p->sprites = p->data->sprt.count;
    p->tpag = p->data->tpag.count;
    p->rooms = p->data->room.count;
    p->code = p->data->code.count;
    p->textures = p->data->txtr.count;
    if (!p->rooms || !p->tpag || !p->code) {
        bs_bootstrap_unload(p);
        return -3;
    }
    p->parserOk = 1;
    return 0;
}
int bs_bootstrap_vm_create(BSBootstrapProbe *p) {
    if (!p || !p->parserOk || !p->data || p->vm) return -1;
    p->vm = VM_create(p->data);
    if (!p->vm) return -2;
    p->vmOk = 1;
    p->vmFunctions = p->vm->funcCallCacheCount;
    p->vmBuiltinsRegistered = p->vm->registeredBuiltinFunctions ? 1 : 0;
    return 0;
}
int bs_bootstrap_runner_create(BSBootstrapProbe *p) {
    if (!p || !p->data || !p->vm || p->runner) return -1;
    p->renderer=BSContractRenderer_create();
    if (!p->renderer) return -2;
    sceIoMkdir("ux0:data/deltarune_saves",0777);
    p->fs=(FileSystem*)OverlayFileSystem_create(
        p->bundleRoot, "ux0:data/deltarune_saves/");
    if (!p->fs) return -3;
    if(p->modRoot[0]){
        SceIoStat modStat;
        if(sceIoGetstat(p->modRoot,&modStat)>=0)
            OverlayFileSystem_setModPath((OverlayFileSystem*)p->fs,p->modRoot);
    }
    p->audio=(AudioSystem*)AlAudioSystem_create();
    if (!p->audio) return -4;
    p->runner=Runner_create(p->data,p->vm,p->renderer,p->fs,p->audio);
    if (!p->runner) return -5;
    p->runnerOk=1;
    return 0;
}
void bs_bootstrap_unload(BSBootstrapProbe *p) {
    if (!p) return;
    if (p->runner) Runner_free(p->runner);
    p->runner = NULL;
    p->runnerOk = 0;
    if (p->renderer) p->renderer->vtable->destroy(p->renderer);
    p->renderer = NULL;
    if (p->audio) p->audio->vtable->destroy(p->audio);
    p->audio = NULL;
    if (p->fs) OverlayFileSystem_destroy((OverlayFileSystem*)p->fs);
    p->fs = NULL;
    if (p->vm) VM_free(p->vm);
    p->vm = NULL;
    p->vmOk = 0;
    if (p->data) DataWin_free(p->data);
    p->data = NULL;
    p->parserOk = 0;
}

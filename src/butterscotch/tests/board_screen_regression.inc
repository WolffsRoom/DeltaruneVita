// Execute the real monitor Draw/with code, counting renderer operations.
static void (*boardOriginalCopy)(Renderer*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, bool);
static bool (*boardOriginalTarget)(Renderer*, int32_t, bool);
static unsigned boardCopies, boardTargets;
static VMContext* boardTestVM;
static void boardCountCopy(Renderer* r, int32_t d, int32_t x, int32_t y, int32_t s,
    int32_t sx, int32_t sy, int32_t w, int32_t h, bool part) {
    if (boardTestVM->currentInstance && boardTestVM->currentInstance->objectIndex >= 0 &&
        strcmp(r->dataWin->objt.objects[boardTestVM->currentInstance->objectIndex].name,"obj_board_screen")==0)
        ++boardCopies;
    boardOriginalCopy(r,d,x,y,s,sx,sy,w,h,part);
}
static bool boardCountTarget(Renderer* r, int32_t s, bool implicit) {
    ++boardTargets;
    return boardOriginalTarget(r,s,implicit);
}
static int testBoardScreens(Runner* runner) {
    RendererVtable* v = runner->renderer->vtable;
    boardTestVM=runner->vmContext;
    boardOriginalCopy=v->surfaceCopy; boardOriginalTarget=v->setRenderTarget;
    v->surfaceCopy=boardCountCopy; v->setRenderTarget=boardCountTarget;
    if(getenv("BS_TEST_BOARD_GRAY")) {
        int object=-1;
        for(uint32_t i=0;i<runner->dataWin->objt.count;i++)
            if(strcmp(runner->dataWin->objt.objects[i].name,"obj_board_grayregion")==0) object=i;
        if(object<0) return EXIT_FAILURE;
        // Exercise the real with(other.screen_surface) path, including a
        // region wholly outside every monitor and one overlapping a monitor.
        Instance* monitor=nullptr;
        for(int i=0;i<arrlen(runner->instances);i++) {
            Instance* inst=runner->instances[i];
            if(inst->objectIndex>=0 && strcmp(runner->dataWin->objt.objects[inst->objectIndex].name,"obj_board_screen")==0) {
                monitor=inst; break;
            }
        }
        if(!monitor) return EXIT_FAILURE;
        Runner_createInstance(runner,-10000,-10000,object);
        Runner_createInstance(runner,monitor->x,monitor->y,object);
    }
    unsigned copies[2]={0},targets[2]={0}, monitors=0;
    for(int pass=0;pass<2;++pass) {
        _putenv_s("BS_TEST_BOARD_SCREEN_CACHE",pass ? "1" : "");
        for(int i=0;i<arrlen(runner->instances);++i) {
            Instance* inst=runner->instances[i];
            inst->boardScreenCache.ready=false;
            if(pass==0 && inst->objectIndex>=0 &&
                strcmp(runner->dataWin->objt.objects[inst->objectIndex].name,"obj_board_screen")==0) ++monitors;
        }
        // Warm caches, then measure one complete two-phase refresh cycle.
        ++runner->frameCount; Runner_draw(runner);
        boardCopies=boardTargets=0;
        for(int i=0;i<2;++i) {
            unsigned before=boardCopies;
            ++runner->frameCount; Runner_draw(runner);
            fprintf(stderr,"BOARD_SCREEN_FRAME pass=%d phase=%d copies=%u\n",pass,i,boardCopies-before);
        }
        copies[pass]=boardCopies; targets[pass]=boardTargets;
    }
    v->surfaceCopy=boardOriginalCopy; v->setRenderTarget=boardOriginalTarget;
    bool ok=monitors>1 && copies[0]>0 && copies[1]*2==copies[0] && targets[1]<targets[0];
    fprintf(stderr,"BOARD_SCREEN_CACHE=%s monitors=%u copies=%u->%u targets=%u->%u\n",
        ok?"pass":"FAIL",monitors,copies[0],copies[1],targets[0],targets[1]);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

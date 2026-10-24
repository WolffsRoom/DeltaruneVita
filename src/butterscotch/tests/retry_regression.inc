// Run against the actual Chapter 3 symbol table, isolated from player saves.
static int testRetry(Runner* runner) {
    int object = -1, room = -1;
    for (uint32_t i=0;i<runner->dataWin->objt.count;i++)
        if (!strcmp(runner->dataWin->objt.objects[i].name,"DEVICE_FAILURE")) object=i;
    for (uint32_t i=0;i<runner->dataWin->room.count;i++)
        if (!strcmp(runner->dataWin->room.rooms[i].name,"PLACE_FAILURE")) room=i;
    if (object < 0 || room < 0) return EXIT_FAILURE;
    Room* savedRoom=runner->currentRoom;
    runner->currentRoom=&runner->dataWin->room.rooms[room];
    Instance* failure=Instance_create(999999,object,0,0);
    VMContext* vm=runner->vmContext;
    int phase=VM_getOrAllocateVarID(vm,"EVENT"), knight=VM_getOrAllocateVarID(vm,"knight_mode");
    int white=VM_getOrAllocateVarID(vm,"WHITEFADE"), speed=VM_getOrAllocateVarID(vm,"FADEUP");
    int chapter=VM_getOrAllocateVarID(vm,"chapter");
    RValue savedChapter=RValue_makeIndependent(Instance_getSelfVar(vm->globalScopeInstance,chapter));
    Instance_setSelfVar(vm->globalScopeInstance,chapter,RValue_makeReal(3));
    Instance_setSelfVar(failure,knight,RValue_makeBool(false));
    Instance_setSelfVar(failure,white,RValue_makeReal(1));
    Instance_setSelfVar(failure,speed,RValue_makeReal(0.01));
    bool ok=true;
    arrput(runner->instances,failure);
    for(int state=0;state<=28;state++) {
        Instance_setSelfVar(failure,phase,RValue_makeReal(state));
        failure->alarm[4]=120;
        failure->activeAlarmMask=1u<<4;
        Runner_shortenRetryFade(runner,failure);
        int expected=state==7?10:state==9?30:120;
        ok &= failure->alarm[4]==expected && failure->activeAlarmMask==(1u<<4);
        ok &= Runner_isFailureGivingUp(runner)==(state==28);
    }
    for(int exempt=0;exempt<3;exempt++) {
        Instance_setSelfVar(failure,phase,RValue_makeReal(9));
        Instance_setSelfVar(failure,knight,RValue_makeBool(exempt==0));
        Instance_setSelfVar(vm->globalScopeInstance,chapter,RValue_makeReal(exempt==1?1:3));
        if(exempt==2) runner->currentRoom=savedRoom;
        failure->alarm[4]=120;
        Runner_shortenRetryFade(runner,failure);
        ok &= failure->alarm[4]==120;
    }
    // Execute the game's actual Step/Alarm/Draw handlers up to scr_tempload,
    // without loading or overwriting a player's save. This catches a stuck
    // state machine or a shortened alarm that leaves the fade incomplete.
    runner->currentRoom=&runner->dataWin->room.rooms[room];
    Instance_setSelfVar(vm->globalScopeInstance,chapter,RValue_makeReal(3));
    Instance_setSelfVar(failure,knight,RValue_makeBool(false));
    Instance_setSelfVar(failure,phase,RValue_makeReal(6));
    Instance_setSelfVar(failure,white,RValue_makeReal(0));
    Instance_setSelfVar(failure,speed,RValue_makeReal(0));
    int factor=VM_getOrAllocateVarID(vm,"FADEFACTOR");
    Instance_setSelfVar(failure,factor,RValue_makeReal(0));
    failure->alarm[4]=-1;
    int steps=0;
    while(steps<160 && Runner_getFailurePhase(runner,failure)!=10) {
        if(failure->alarm[4]>0 && --failure->alarm[4]==0) {
            failure->alarm[4]=-1;
            Runner_executeEvent(runner,failure,EVENT_ALARM,4);
        }
        if(Runner_getFailurePhase(runner,failure)==10) break;
        Runner_executeEvent(runner,failure,EVENT_STEP,STEP_NORMAL);
        Runner_executeEvent(runner,failure,EVENT_DRAW,DRAW_NORMAL);
        steps++;
    }
    double fade=RValue_toReal(Instance_getSelfVar(failure,factor));
    fprintf(stderr,"RETRY_GML steps=%d phase=%d fade=%.4f\n",steps,Runner_getFailurePhase(runner,failure),fade);
    ok &= steps==40 && Runner_getFailurePhase(runner,failure)==10 && fade>=0.99;
    arrpop(runner->instances);
    runner->currentRoom=savedRoom;
    Instance_setSelfVar(vm->globalScopeInstance,chapter,savedChapter);
    RValue_free(&savedChapter);
    Instance_free(failure);

    OverlayFileSystem* overlay=OverlayFileSystem_create("scratch/retry-fs/bundle","scratch/retry-fs/save");
    OverlayFileSystem_setModPath(overlay,"scratch/retry-fs/mod");
    FileSystem* fs=(FileSystem*)overlay;
    const char* names[]={"filech3_9","filech3_0","translation.txt","filech5_9"};
    const char* expected[]={"live checkpoint","live save","mod translation","bundled checkpoint"};
    for(int i=0;i<4;i++) {
        char* text=fs->vtable->readFileText(fs,names[i]);
        ok &= text && !strcmp(text,expected[i]);
        free(text);
    }
    ok &= fs->vtable->writeFileText(fs,"filech3_9","new checkpoint");
    char* text=fs->vtable->readFileText(fs,"filech3_9");
    ok &= text && !strcmp(text,"new checkpoint");
    free(text);
    OverlayFileSystem_destroy(overlay);
    fprintf(stderr,"RETRY_REGRESSION=%s states=29 exclusions=3 file_checks=5 continue_delay=150_to_40_steps\n",ok?"pass":"FAIL");
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}

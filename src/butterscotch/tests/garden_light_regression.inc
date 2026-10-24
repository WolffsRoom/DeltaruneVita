typedef struct {
    int32_t page, sx, sy, sw, sh;
    float x, y, xs, ys, angle, px, py, alpha;
    uint32_t color;
} GardenDraw;
static GardenDraw* gardenDraws;
static void recordGardenDraw(Renderer* r, int32_t p, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                             float x, float y, float xs, float ys, float angle, float px, float py, uint32_t color, float alpha) {
    GardenDraw d = {p,sx,sy,sw,sh,x,y,xs,ys,angle,px,py,alpha,color};
    arrput(gardenDraws, d);
}
static int testGardenLight(Runner* runner) {
    VMContext* vm = runner->vmContext;
    int codeId = -1;
    for (uint32_t i=0; i<runner->dataWin->code.count; ++i)
        if (runner->dataWin->code.entries[i].name && strcmp(runner->dataWin->code.entries[i].name,"gml_Object_obj_gardenlight_Draw_0")==0) codeId=i;
    if (codeId<0) return EXIT_FAILURE;
    Renderer* r = runner->renderer;
    r->vtable->flush(r);
    RendererVtable original = *r->vtable;
    r->vtable->drawSpritePart = recordGardenDraw;
    bool ok = true;
    int checked=0, totalDraws=0;
    uint64_t gmlNs=0, nativeNs=0;
    for (size_t i=0;i<arrlenu(runner->instances);++i) {
        Instance* inst=runner->instances[i];
        if (inst->destroyed || strcmp(runner->dataWin->objt.objects[inst->objectIndex].name,"obj_gardenlight")) continue;
        vm->currentInstance=inst;
        for (int pass=0;pass<3;++pass) {
            int opacity=shget(vm->varNameMap,"opacity_value");
            Instance_setSelfVar(inst,opacity,RValue_makeReal(pass*0.5));
            arrsetlen(gardenDraws,0); r->drawAlpha=0.37f; r->drawColor=0;
            uint64_t start=nowNanos();
            RValue result=VM_executeCode(vm,codeId); RValue_free(&result);
            gmlNs+=nowNanos()-start;
            GardenDraw* reference=gardenDraws; gardenDraws=nullptr;
            float alpha=r->drawAlpha; uint32_t color=r->drawColor;
            r->drawAlpha=0.37f; r->drawColor=0;
            start=nowNanos();
            bool handled=VMBuiltins_tryGardenLightDraw(vm,inst,codeId);
            if (!handled && checked==0) {
                CodeEntry* ce=&runner->dataWin->code.entries[codeId];
                const uint8_t* bytes=runner->dataWin->bytecodeBuffer+ce->bytecodeAbsoluteOffset-runner->dataWin->bytecodeBufferBase;
                uint32_t hash=2166136261U; for(uint32_t k=0;k<ce->length;++k) hash=(hash^bytes[k])*16777619U;
                fprintf(stderr,"GARDEN_NATIVE_SKIP match=%u length=%u hash=%08x\n",ce->gardenLightNativeMatch,ce->length,hash);
            }
            nativeNs+=nowNanos()-start;
            bool same=handled && arrlenu(reference)==arrlenu(gardenDraws) && alpha==r->drawAlpha && color==r->drawColor;
            if (same && arrlenu(reference)) same=memcmp(reference,gardenDraws,arrlenu(reference)*sizeof(GardenDraw))==0;
            ok &= same; totalDraws+=(int)arrlen(reference); checked++;
            arrfree(reference); arrfree(gardenDraws);
        }
    }
    *r->vtable=original;
    fprintf(stderr,"GARDEN_LIGHT_EQUIVALENCE=%s cases=%d draws=%d gml_us=%llu native_us=%llu\n",
            ok && checked && totalDraws ? "pass":"FAIL",checked,totalDraws,
            (unsigned long long)(gmlNs/1000),(unsigned long long)(nativeNs/1000));
    return ok && checked && totalDraws ? EXIT_SUCCESS : EXIT_FAILURE;
}

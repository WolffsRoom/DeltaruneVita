// Compare actual game getters against the interpreter, including ownership.
static int testSelfGetters(Runner* runner) {
    VMContext* vm = runner->vmContext;
    Instance* saved = vm->currentInstance;
    unsigned checked = 0;
    for (uint32_t c = 0; c < vm->dataWin->code.count; ++c) {
        CodeEntry* code = &vm->dataWin->code.entries[c];
        if (code->name && strstr(code->name,"obj_dw_teevie_tv_screens") && strstr(code->name,"Draw_0")) {
            const uint8_t* b=vm->dataWin->bytecodeBuffer+code->bytecodeAbsoluteOffset-vm->dataWin->bytecodeBufferBase;
            uint32_t h=2166136261U;
            for(uint32_t i=0;i<code->length;++i) h=(h^b[i])*16777619U;
            fprintf(stderr,"TV_DRAW_SIGNATURE name=%s length=%u hash=%08x\n",code->name,code->length,h);
        }
    }
    uint64_t timings[2] = {0};
    for (uint32_t c = 0; c < vm->dataWin->code.count; ++c) {
        CodeEntry* code = &vm->dataWin->code.entries[c];
        if (code->selfGetterVar <= 0) continue;
        int var = vm->dataWin->vari.variables[code->selfGetterVar - 1].varID;
        for (int s = 0; s < arrlen(runner->structInstances); ++s) {
            Instance* inst = runner->structInstances[s];
            if (!IntRValueHashMap_findSlot(&inst->selfVars, var)) continue;
            vm->currentInstance = inst;
            int stack = vm->stack.top, depth = vm->callDepth;
            vm->disableSelfGetterFastPath = true;
            RValue expected = VM_callCodeIndex(vm, c, nullptr, 0);
            vm->disableSelfGetterFastPath = false;
            RValue actual = VM_callCodeIndex(vm, c, nullptr, 0);
            bool ok = RValue_deepEqual(expected, actual, 0) && expected.ownsReference == actual.ownsReference &&
                      expected.gmlStackType == actual.gmlStackType &&
                      vm->stack.top == stack && vm->callDepth == depth && vm->currentInstance == inst;
            RValue_free(&expected); RValue_free(&actual);
            if (!ok) { fprintf(stderr,"SELF_GETTER=FAIL code=%s\n",code->name); return EXIT_FAILURE; }
            if (checked == 0) {
                Profiler* savedProfiler = vm->profiler;
                vm->profiler = Profiler_create();
                for (int sample=0;sample<2;++sample) {
                    Profiler_setSampling(vm->profiler,sample != 0);
                    uint64_t before=vm->selfGetterFastCalls;
                    RValue profiled=VM_callCodeIndex(vm,c,nullptr,0);
                    RValue_free(&profiled);
                    if ((vm->selfGetterFastCalls > before) != (sample == 0)) {
                        fprintf(stderr,"SELF_GETTER=FAIL profiler sampling=%d\n",sample);
                        return EXIT_FAILURE;
                    }
                }
                Profiler_destroy(vm->profiler);
                vm->profiler=savedProfiler;
                RValue* slot = IntRValueHashMap_findSlot(&inst->selfVars,var);
                RValue savedValue = RValue_makeIndependent(*slot);
                RValue values[] = { RValue_makeReal(7.5), RValue_makeInt32(-12), RValue_makeBool(true),
                    RValue_makeOwnedString(safeStrdup("getter ownership")),
                    RValue_makeArray(GMLArray_create(vm->dataWin->gen8.wadVersion,3)),
                    RValue_makeStructAndIncRef(inst) };
                for (int v=0;v<6;++v) {
                    RValue_writeIntoSlotStealingOwnershipOrCopying(slot,values[v]);
                    vm->disableSelfGetterFastPath=true;
                    RValue a=VM_callCodeIndex(vm,c,nullptr,0);
                    vm->disableSelfGetterFastPath=false;
                    RValue b=VM_callCodeIndex(vm,c,nullptr,0);
                    bool equal=RValue_deepEqual(a,b,0) && a.ownsReference==b.ownsReference && a.gmlStackType==b.gmlStackType;
                    RValue_free(&a); RValue_free(&b);
                    if (!equal) { fprintf(stderr,"SELF_GETTER=FAIL ownership case=%d\n",v); return EXIT_FAILURE; }
                }
                RValue_writeIntoSlotStealingOwnershipOrCopying(slot,savedValue);
            }
            for (int pass = 0; pass < 2; ++pass) {
                vm->disableSelfGetterFastPath = pass == 0;
                uint64_t start = nowNanos();
                for (int i = 0; i < 1000; ++i) {
                    RValue value = VM_callCodeIndex(vm, c, nullptr, 0);
                    RValue_free(&value);
                }
                timings[pass] += nowNanos() - start;
            }
            ++checked;
            break;
        }
    }
    vm->currentInstance = saved;
    vm->disableSelfGetterFastPath = false;
    fprintf(stderr,"SELF_GETTER=%s checked=%u calls=%llu baseline_ns=%llu fast_ns=%llu\n",
        checked ? "pass" : "FAIL", checked, (unsigned long long)vm->selfGetterFastCalls,
        (unsigned long long)timings[0], (unsigned long long)timings[1]);
    return checked ? EXIT_SUCCESS : EXIT_FAILURE;
}

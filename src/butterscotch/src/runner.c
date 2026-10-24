#include "runner.h"
#ifdef PLATFORM_VITA
#include "vita_room_prefetch.h"
#endif
#include "data_win.h"
#include "instance.h"
#include "renderer.h"
#include "vm.h"
#include "vm_builtins.h"
#include "utils.h"
#include "json_writer.h"
#include "collision.h"
#ifdef PLATFORM_VITA
#include <vitaGL.h>
#include "vita_gpu_rules.h"
#include "gl_legacy/gl_legacy_renderer.h"
#include "gl/gl_renderer.h"
#include "audio/openal/al_audio_system.h"
extern int g_vitaActiveChapter;
extern bool g_vitaModernGlActive;
extern bool g_vitaExperimentalGlActive;
extern void VitaProbe_logLine(const char* text);
// Chapter 1 frequently sends the player straight back to the previous room.
// Keep exactly that one lazy ROOM payload parsed; it is small compared with
// texture pages and avoids reparsing it on every field/forest boundary.
static int32_t vitaCh1PreviousRoomPayload = -1;

// World actors that remain alive underneath DELTARUNE's full-screen battle
// overlay. Drawing them on Vita is invisible work and, more importantly,
// requests the exploration atlases again while battle atlases are resident.
static bool vitaIsCh2ExplorationDrawObject(const char* name) {
    if (name == nullptr) return false;
    static const char* const hiddenDuringBattle[] = {
        "obj_mainchara",
        "obj_actor",
        "obj_caterpillarchara",
        "obj_darkcontroller",
        "obj_cyber_wall_lights",
        "obj_ch2_capsule",
        "obj_parallaxer_layer_cyber"
    };
    for (uint32_t i = 0;
         i < sizeof(hiddenDuringBattle) / sizeof(hiddenDuringBattle[0]); ++i) {
        if (strcmp(name, hiddenDuringBattle[i]) == 0) return true;
    }
    return false;
}

// The music-bullet room keeps many projectile instances alive outside the
// active camera. Their Step events must continue so patterns remain correct,
// but executing their GML Draw bytecode before the renderer rejects the final
// quad wastes measurable CPU time. Cull only fully off-camera projectile
// instances, with a generous margin; partially visible sprites are untouched.
static bool vitaCullCh2MusicBulletDraw(Runner* runner, Instance* inst,
                                       const char* objectName) {
    if (runner == nullptr || inst == nullptr || objectName == nullptr ||
        runner->currentRoom == nullptr || runner->currentRoom->name == nullptr ||
        strcmp(runner->currentRoom->name, "room_dw_cyber_music_bullet") != 0)
        return false;
    if (strcmp(objectName, "obj_beatbullet_2") != 0 &&
        strcmp(objectName, "obj_teacup_bullet") != 0 &&
        strcmp(objectName, "obj_teacup") != 0)
        return false;
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    if (camera == nullptr || !camera->allocated || camera->viewWidth <= 0 ||
        camera->viewHeight <= 0) return false;
    const float margin = 96.0f;
    float left = (float)camera->viewX - margin;
    float top = (float)camera->viewY - margin;
    float right = (float)(camera->viewX + camera->viewWidth) + margin;
    float bottom = (float)(camera->viewY + camera->viewHeight) + margin;
    InstanceBBox bbox = Collision_computeBBox(runner, inst);
    if (bbox.valid)
        return bbox.right < left || bbox.left > right ||
               bbox.bottom < top || bbox.top > bottom;
    return inst->x < left || inst->x > right || inst->y < top || inst->y > bottom;
}
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "math_compat.h"

#include "debug_overlay.h"
#include "gettime.h"
#include "stb_ds.h"

void Runner_dispatchVideoAsync(Runner* runner, const char* type) {
    if (runner == nullptr || type == nullptr) return;

    DsMapEntry* map = nullptr;
    arrput(runner->dsMapPool, map);
    int32_t mapId = arrlen(runner->dsMapPool) - 1;
    DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
    shput(*mapPtr, safeStrdup("type"), RValue_makeOwnedString(safeStrdup(type)));

    runner->asyncLoadMapId = mapId;
    Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ASYNC_VIDEO);

    mapPtr = &runner->dsMapPool[mapId];
    if (*mapPtr != nullptr) {
        repeat(shlen(*mapPtr), i) {
            free((*mapPtr)[i].key);
            RValue_free(&(*mapPtr)[i].value);
        }
        shfree(*mapPtr);
        *mapPtr = nullptr;
    }
    runner->asyncLoadMapId = -1;
}

// ===[ Runtime Layer Teardown Helpers ]===
void Runner_freeRuntimeLayer(RuntimeLayer* runtimeLayer) {
    free(runtimeLayer->dynamicName);
    runtimeLayer->dynamicName = nullptr;
    size_t elementCount = arrlenu(runtimeLayer->elements);
    repeat(elementCount, i) {
        RuntimeLayerElement* el = &runtimeLayer->elements[i];
        if (el->backgroundElement != nullptr) {
            free(el->backgroundElement);
            el->backgroundElement = nullptr;
        }
        if (el->spriteElement != nullptr) {
            free(el->spriteElement);
            el->spriteElement = nullptr;
        }
    }
    arrfree(runtimeLayer->elements);
    runtimeLayer->elements = nullptr;
}

static void freeRuntimeLayersArray(RuntimeLayer** runtimeLayerArray) {
    size_t count = arrlenu(*runtimeLayerArray);
    repeat(count, i) {
        Runner_freeRuntimeLayer(&(*runtimeLayerArray)[i]);
    }
    arrfree(*runtimeLayerArray);
    *runtimeLayerArray = nullptr;
}

void Runner_updateCameraViewSimple(GMLCamera* camera) {

    float x = camera->viewX + camera->viewWidth/2;
    float y = camera->viewY + camera->viewHeight/2;
    Matrix4f viewMatrix;
    Matrix4f_identity(&viewMatrix);
    Matrix4f_LookAt(&viewMatrix, x, y, -16000.0, x, y, 16000.0, 0.0, 1.0, 0.0);
    Matrix4f_translate(&viewMatrix, x, y, 0.0f);
    Matrix4f_rotateZ(&viewMatrix, -camera->viewAngle * (float) M_PI / 180.0f);
    Matrix4f_translate(&viewMatrix, -x, -y, 0.0f);

    Matrix4f projectionMatrix;
    Matrix4f_Orthographic(&projectionMatrix, (float) camera->viewWidth, -((float) camera->viewHeight), 32000.0, 0.0);
    

    camera->viewMatrix = viewMatrix;
    camera->projectionMatrix = projectionMatrix;

}

// ===[ Helper: Find event action in object hierarchy ]===
// Resolves the handler for (objectIndex, eventType, eventSubtype) via the precomputed ResolvedEventTable.
// Returns the CODE chunk handler id, or -1 if the object does not respond.
// If outOwnerObjectIndex is non-null, it is set to the resolved owner objectIndex (-1 if not found).
static int32_t findEventCodeIdAndOwner(Runner* runner, int32_t objectIndex, int32_t eventType, int32_t eventSubtype, int32_t* outOwnerObjectIndex) {
    int32_t slot = EventSlotMap_lookup(&runner->eventSlotMap, eventType, eventSubtype);
    if (0 > slot) {
        if (outOwnerObjectIndex != nullptr) *outOwnerObjectIndex = -1;
        return -1;
    }
    return ResolvedEventTable_lookup(&runner->eventTable, objectIndex, slot, outOwnerObjectIndex);
}

// ===[ Per-Object Instance Lists ]===
// Each instance lives in the list of its own object and every ancestor object (descendant-inclusive).
// This mirrors the native runner and lets collision dispatch iterate only the candidate instances for a target object, instead of scanning the whole room per collision event.
// The difference is that the native runner uses a linked list, while we move things manually with memmove.

void Runner_addInstanceToObjectLists(Runner* runner, Instance* inst) {
    DataWin* dataWin = runner->dataWin;
    int32_t currentObj = inst->objectIndex;
    int32_t depth = 0;
    while (currentObj >= 0 && dataWin->objt.count > (uint32_t) currentObj && 32 > depth) {
        arrput(runner->instancesByObject[currentObj], inst);
        currentObj = dataWin->objt.objects[currentObj].parentId;
        depth++;
    }
    if (inst->objectIndex >= 0 && dataWin->objt.count > (uint32_t) inst->objectIndex) {
        arrput(runner->instancesByExactObject[inst->objectIndex], inst);
    }
    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
}

// Stable remove of inst from list, preserving creation order. Returns true if removed.
static bool removeInstanceFromList(Instance*** listPtr, Instance* inst) {
    Instance** list = *listPtr;
    int32_t n = (int32_t) arrlen(list);
    repeat(n, i) {
        if (list[i] == inst) {
            if (n - 1 > i) memmove(&list[i], &list[i + 1], (size_t) (n - 1 - i) * sizeof(Instance*));
            arrsetlen(*listPtr, n - 1);
            return true;
        }
    }
    return false;
}

void Runner_removeInstanceFromObjectLists(Runner* runner, Instance* inst) {
    DataWin* dataWin = runner->dataWin;
    int32_t currentObj = inst->objectIndex;
    int32_t depth = 0;
    while (currentObj >= 0 && dataWin->objt.count > (uint32_t) currentObj && 32 > depth) {
        removeInstanceFromList(&runner->instancesByObject[currentObj], inst);
        currentObj = dataWin->objt.objects[currentObj].parentId;
        depth++;
    }
    if (inst->objectIndex >= 0 && dataWin->objt.count > (uint32_t) inst->objectIndex) {
        removeInstanceFromList(&runner->instancesByExactObject[inst->objectIndex], inst);
    }
}

void Runner_clearAllObjectLists(Runner* runner) {
    if (runner->instancesByObject == nullptr) return;
    uint32_t count = runner->dataWin->objt.count;
    repeat(count, i) {
        arrsetlen(runner->instancesByObject[i], 0);
        if (runner->instancesByExactObject != nullptr) {
            arrsetlen(runner->instancesByExactObject[i], 0);
        }
    }
}

int32_t Runner_pushInstancesOfObject(Runner* runner, int32_t targetObjIndex) {
    int32_t base = (int32_t) arrlen(runner->instanceSnapshots);

    if (targetObjIndex == INSTANCE_ALL) {
        int32_t instanceCount = (int32_t) arrlen(runner->instances);
        arrsetlen(runner->instanceSnapshots, base + instanceCount);
        memcpy(&runner->instanceSnapshots[base], runner->instances, (size_t) instanceCount * sizeof(Instance*));
        return base;
    }

    if (0 > targetObjIndex || (uint32_t) targetObjIndex >= runner->dataWin->objt.count)
        return base;

    Instance** source = runner->instancesByObject[targetObjIndex];
    int32_t sourceCount = (int32_t) arrlen(source);

    if (0 >= sourceCount)
        return base;

    arrsetlen(runner->instanceSnapshots, base + sourceCount);
    memcpy(&runner->instanceSnapshots[base], source, (size_t) sourceCount * sizeof(Instance*));
    return base;
}

void Runner_popInstanceSnapshot(Runner* runner, int32_t base) {
    arrsetlen(runner->instanceSnapshots, base);
}

int32_t Runner_pushInstancesForTarget(Runner* runner, int32_t target) {
    int32_t base = (int32_t) arrlen(runner->instanceSnapshots);
    if (target >= 0 && 100000 > target) {
        return Runner_pushInstancesOfObject(runner, target);
    }
    if (target == INSTANCE_ALL) {
        int32_t total = (int32_t) arrlen(runner->instances);
        if (0 >= total)
            return base;
        arrsetlen(runner->instanceSnapshots, base + total);
        memcpy(&runner->instanceSnapshots[base], runner->instances, (size_t) total * sizeof(Instance*));
        return base;
    }
    if (target >= INSTANCE_ID_BASE) {
        Instance* inst = hmget(runner->instancesById, target);
        if (inst != nullptr) arrput(runner->instanceSnapshots, inst);
        return base;
    }
    return base;
}

// ===[ Event Execution ]===

static void setVMInstanceContext(VMContext* vm, Instance* instance) {
    vm->currentInstance = instance;
}

static void restoreVMInstanceContext(VMContext* vm, Instance* savedInstance) {
    vm->currentInstance = savedInstance;
}

static void executeCode(Runner* runner, Instance* instance, int32_t codeId) {
    // GameMaker does use codeIds less than 0, we'll just pretend we didn't hear them...
    if (0 > codeId) return;

    VMContext* vm = runner->vmContext;

    // Save instance context
    Instance* savedInstance = (Instance*) vm->currentInstance;

    // Save full VM execution state, because VM_executeCode overwrites all of these.
    // This is necessary for nested execution (e.g., instance_create triggering a Create
    // event while another event's executeLoop is still on the call stack).
    uint8_t* savedBytecodeBase = vm->bytecodeBase;
    uint32_t savedIP = vm->ip;
    uint32_t savedCodeEnd = vm->codeEnd;
    const char* savedCodeName = vm->currentCodeName;
    RValue* savedLocalVars = vm->localVars;
    uint32_t savedLocalVarCount = vm->localVarCount;
    IntIntHashMap* savedCodeLocalsSlotMap = vm->currentCodeLocalsSlotMap;
    int32_t savedCodeIndex = vm->currentCodeIndex;
    int32_t savedStackTop = vm->stack.top;

    // Save stack values (VM_executeCode resets stack.top to 0, which would let
    // the nested execution overwrite the caller's stack slot values)
    RValue* savedStackValues = nullptr;
    if (savedStackTop > 0) {
        savedStackValues = (RValue *)safeMalloc((uint32_t) savedStackTop * sizeof(RValue));
        memcpy(savedStackValues, vm->stack.slots, (uint32_t) savedStackTop * sizeof(RValue));
    }

    // Set instance context
    setVMInstanceContext(vm, instance);

    // Execute
    RValue result = VM_executeCode(vm, codeId);
    RValue_free(&result);

    // Restore instance context
    restoreVMInstanceContext(vm, savedInstance);

    // Restore VM execution state
    vm->bytecodeBase = savedBytecodeBase;
    vm->ip = savedIP;
    vm->codeEnd = savedCodeEnd;
    vm->currentCodeName = savedCodeName;
    vm->localVars = savedLocalVars;
    vm->localVarCount = savedLocalVarCount;
    vm->currentCodeLocalsSlotMap = savedCodeLocalsSlotMap;
    vm->currentCodeIndex = savedCodeIndex;
    vm->stack.top = savedStackTop;

    // Restore stack values
    if (savedStackTop > 0) {
        memcpy(vm->stack.slots, savedStackValues, (uint32_t) savedStackTop * sizeof(RValue));
        free(savedStackValues);
    }
}

const char* Runner_getEventName(int32_t eventType, int32_t eventSubtype) {
    switch (eventType) {
        case EVENT_CREATE:  return "Create";
        case EVENT_DESTROY: return "Destroy";
        case EVENT_ALARM:   return "Alarm";
        case EVENT_COLLISION: return "Collision";
        case EVENT_STEP:
            switch (eventSubtype) {
                case STEP_BEGIN:  return "BeginStep";
                case STEP_NORMAL: return "NormalStep";
                case STEP_END:    return "EndStep";
                default:          return "Step";
            }
        case EVENT_DRAW:
            switch (eventSubtype) {
                case DRAW_NORMAL:    return "Draw";
                case DRAW_GUI:       return "DrawGUI";
                case DRAW_BEGIN:     return "DrawBegin";
                case DRAW_END:       return "DrawEnd";
                case DRAW_GUI_BEGIN: return "DrawGUIBegin";
                case DRAW_GUI_END:   return "DrawGUIEnd";
                case DRAW_PRE:       return "DrawPre";
                case DRAW_POST:      return "DrawPost";
                default:             return "Draw";
            }
        case EVENT_KEYBOARD:   return "Keyboard";
        case EVENT_MOUSE:
            switch (eventSubtype) {
                case MOUSE_LEFT_BUTTON:          return "MouseLeftButton";
                case MOUSE_RIGHT_BUTTON:         return "MouseRightButton";
                case MOUSE_MIDDLE_BUTTON:        return "MouseMiddleButton";
                case MOUSE_NO_BUTTON:            return "MouseNoButton";
                case MOUSE_LEFT_PRESSED:         return "MouseLeftPressed";
                case MOUSE_RIGHT_PRESSED:        return "MouseRightPressed";
                case MOUSE_MIDDLE_PRESSED:       return "MouseMiddlePressed";
                case MOUSE_LEFT_RELEASED:        return "MouseLeftReleased";
                case MOUSE_RIGHT_RELEASED:       return "MouseRightReleased";
                case MOUSE_MIDDLE_RELEASED:      return "MouseMiddleReleased";
                case MOUSE_ENTER:                return "MouseEnter";
                case MOUSE_LEAVE:                return "MouseLeave";
                case MOUSE_GLOB_LEFT_BUTTON:     return "GlobalLeftButton";
                case MOUSE_GLOB_RIGHT_BUTTON:    return "GlobalRightButton";
                case MOUSE_GLOB_MIDDLE_BUTTON:   return "GlobalMiddleButton";
                case MOUSE_GLOB_LEFT_PRESSED:    return "GlobalLeftPressed";
                case MOUSE_GLOB_RIGHT_PRESSED:   return "GlobalRightPressed";
                case MOUSE_GLOB_MIDDLE_PRESSED:  return "GlobalMiddlePressed";
                case MOUSE_GLOB_LEFT_RELEASED:   return "GlobalLeftReleased";
                case MOUSE_GLOB_RIGHT_RELEASED:  return "GlobalRightReleased";
                case MOUSE_GLOB_MIDDLE_RELEASED: return "GlobalMiddleReleased";
                case MOUSE_WHEEL_UP:             return "MouseWheelUp";
                case MOUSE_WHEEL_DOWN:           return "MouseWheelDown";
                default:                         return "Mouse";
            }
        case EVENT_OTHER:
            switch (eventSubtype) {
                case OTHER_OUTSIDE_ROOM:    return "OutsideRoom";
                case OTHER_GAME_START:      return "GameStart";
                case OTHER_ROOM_START:      return "RoomStart";
                case OTHER_ROOM_END:        return "RoomEnd";
                case OTHER_NO_MORE_LIVES:   return "NoMoreLives";
                case OTHER_ANIMATION_END:   return "AnimationEnd";
                case OTHER_END_OF_PATH:     return "EndOfPath";
                case OTHER_NO_MORE_HEALTH:  return "NoMoreHealth";
                case OTHER_USER0 +  0:      return "UserEvent0";
                case OTHER_USER0 +  1:      return "UserEvent1";
                case OTHER_USER0 +  2:      return "UserEvent2";
                case OTHER_USER0 +  3:      return "UserEvent3";
                case OTHER_USER0 +  4:      return "UserEvent4";
                case OTHER_USER0 +  5:      return "UserEvent5";
                case OTHER_USER0 +  6:      return "UserEvent6";
                case OTHER_USER0 +  7:      return "UserEvent7";
                case OTHER_USER0 +  8:      return "UserEvent8";
                case OTHER_USER0 +  9:      return "UserEvent9";
                case OTHER_USER0 + 10:      return "UserEvent10";
                case OTHER_USER0 + 11:      return "UserEvent11";
                case OTHER_USER0 + 12:      return "UserEvent12";
                case OTHER_USER0 + 13:      return "UserEvent13";
                case OTHER_USER0 + 14:      return "UserEvent14";
                case OTHER_USER0 + 15:      return "UserEvent15";
                case OTHER_OUTSIDE_VIEW0:   return "OutsideView0";
                case OTHER_OUTSIDE_VIEW1:   return "OutsideView1";
                case OTHER_OUTSIDE_VIEW2:   return "OutsideView2";
                case OTHER_OUTSIDE_VIEW3:   return "OutsideView3";
                case OTHER_OUTSIDE_VIEW4:   return "OutsideView4";
                case OTHER_OUTSIDE_VIEW5:   return "OutsideView5";
                case OTHER_OUTSIDE_VIEW6:   return "OutsideView6";
                case OTHER_OUTSIDE_VIEW7:   return "OutsideView7";
                default:                    return "Other";
            }
        case EVENT_KEYPRESS:   return "KeyPress";
        case EVENT_KEYRELEASE: return "KeyRelease";
        case EVENT_PRECREATE:  return "PreCreate";
        case EVENT_CLEANUP: return "Clean Up";
        default: return "Unknown";
    }
}

// Some events check if there's a pending room and, if there is, the events are NOT dispatched.
// Persistent instances (or instances in a persistent room) still receive Create / Destroy / Alarm / Other / PreCreate so cleanup hooks still run.
// This mirrors what the official YoYo runner does.
static bool isEventBlockedByPendingRoom(Runner* runner, Instance* instance, int32_t eventType) {
    if (0 > runner->pendingRoom)
        return false;

    bool persistent = (instance != nullptr && instance->persistent) || (runner->currentRoom != nullptr && runner->currentRoom->persistent);
    if (!persistent)
        return true;

    if (eventType == EVENT_CREATE || eventType == EVENT_DESTROY || eventType == EVENT_ALARM || eventType == EVENT_OTHER || eventType == EVENT_PRECREATE)
        return false;

    return true;
}

#ifdef PLATFORM_VITA
static void vitaEnableChapter5CliffSimpleMode(Runner* runner, Instance* instance,
                                               int32_t eventType) {
    if (g_vitaActiveChapter != 5 || eventType != EVENT_CREATE ||
        runner == nullptr || instance == nullptr || runner->vmContext == nullptr ||
        instance->objectIndex < 0 ||
        (uint32_t)instance->objectIndex >= runner->dataWin->objt.count)
        return;
    const char* roomName = runner->currentRoom != nullptr ?
                           runner->currentRoom->name : nullptr;
    if (!VitaGpuRules_ch5CliffSimpleMode(roomName)) return;
    const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
    if (objectName == nullptr ||
        strcmp(objectName, "obj_parallax_cliffs_moving_layers") != 0)
        return;

    VMContext* vm = runner->vmContext;
    int32_t simpleId = VM_getOrAllocateVarID(vm, "simple");
    int32_t alphaId = VM_getOrAllocateVarID(vm, "layer_alpha");
    Instance_setSelfVar(instance, simpleId, RValue_makeBool(true));
    RValue alphas = Instance_getSelfVar(instance, alphaId);
    if (alphas.type == RVALUE_ARRAY && alphas.array != nullptr) {
        // This is the object's own Simple profile: retain the principal far
        // and near layers, omit the gradient/cave duplicates, and skip its
        // expensive palette-mapper pass. Gameplay geometry is unaffected.
        GMLArray_set(alphas.array, 1, RValue_makeReal(0.0));
        GMLArray_set(alphas.array, 3, RValue_makeReal(0.0));
    }
    VitaProbe_logLine("CH5_CLIFF_SIMPLE=enabled object=obj_parallax_cliffs_moving_layers");
}
#endif

static RValue failureField(Runner* runner, Instance* instance, const char* name) {
    ptrdiff_t slot = shgeti(runner->vmContext->varNameMap, name);
    return slot >= 0 ? Instance_getSelfVar(instance,
        runner->vmContext->varNameMap[slot].value) : RValue_makeUndefined();
}

int32_t Runner_getFailurePhase(Runner* runner, Instance* instance) {
    if (!runner || !instance || !runner->currentRoom ||
        !runner->currentRoom->name || strcmp(runner->currentRoom->name, "PLACE_FAILURE") != 0 ||
        instance->destroyed || instance->objectIndex < 0 ||
        (uint32_t)instance->objectIndex >= runner->dataWin->objt.count)
        return -1;
    const char* name = runner->dataWin->objt.objects[instance->objectIndex].name;
    if (!name || strcmp(name, "DEVICE_FAILURE") != 0) return -1;
    RValue phase = failureField(runner, instance, "EVENT");
    return phase.type == RVALUE_UNDEFINED ? -1 : RValue_toInt32(phase);
}

bool Runner_isFailureGivingUp(Runner* runner) {
    if (!runner) return false;
    repeat((int32_t)arrlen(runner->instances), i) {
        if (Runner_getFailurePhase(runner, runner->instances[i]) == 28) return true;
    }
    return false;
}

void Runner_shortenRetryFade(Runner* runner, Instance* instance) {
    int phase = Runner_getFailurePhase(runner, instance);
    if (phase != 7 && phase != 9) return;
    RValue chapter = failureField(runner, runner->vmContext->globalScopeInstance, "chapter");
    RValue knight = failureField(runner, instance, "knight_mode");
    if (chapter.type == RVALUE_UNDEFINED || RValue_toInt32(chapter) < 2 ||
        RValue_toInt32(chapter) > 5 || knight.type == RVALUE_UNDEFINED || RValue_toBool(knight)) return;
    // Preserve the GML alarm/event sequence and scr_tempload(), including
    // inventory, boss flags and checkpoint selection. Only shorten presentation.
    int limit = phase == 7 ? 10 : 30;
    if (instance->alarm[4] <= limit) return;
    if (phase == 9) {
        RValue white = failureField(runner, instance, "WHITEFADE");
        RValue speed = failureField(runner, instance, "FADEUP");
        if (white.type == RVALUE_UNDEFINED || !RValue_toBool(white) ||
            speed.type == RVALUE_UNDEFINED || RValue_toReal(speed) <= 0) return;
        if (RValue_toReal(speed) < 1.0 / 30.0)
            Instance_setSelfVar(instance, shget(runner->vmContext->varNameMap, "FADEUP"),
                                RValue_makeReal(1.0 / 30.0));
    }
    instance->alarm[4] = limit;
#ifdef PLATFORM_VITA
    VitaProbe_logLine(phase == 7 ? "RETRY=continue_wait_shortened steps=10" :
                                  "RETRY=continue_fade_shortened steps=30 checkpoint=original_gml");
#endif
}

// Executes an already-resolved event handler (see findEventCodeIdAndOwner) and verified codeId >= 0.
static void Runner_executeResolvedEvent(Runner* runner, Instance* instance, int32_t eventType, int32_t eventSubtype, int32_t codeId, int32_t ownerObjectIndex) {
    if (isEventBlockedByPendingRoom(runner, instance, eventType))
        return;

#ifdef PLATFORM_VITA
    // The legacy renderer cannot apply Chapter 5's shader_replace_simple LUT.
    // Executing obj_lutshader nevertheless copied the full 640x480
    // application surface to another FBO and back every frame. Skip that
    // visually neutral pass on Vita; shadows and all gameplay draws remain.
    extern int g_vitaActiveChapter;
    extern int g_vitaSpanishModActive;
    extern int g_vitaShadersEnabled;
    // The Spanish translation replaces obj_time's Post Draw with a PC
    // fullscreen compositor that stretches application_surface a second
    // time. Vita already composites and centers that surface natively, so
    // executing the replacement squeezes the game and lets GUI elements
    // escape the 4:3 viewport. Keep the Spanish data and skip only this PC
    // presentation pass in Chapters 1-5.
    if (g_vitaSpanishModActive && eventType == EVENT_DRAW && eventSubtype == DRAW_POST &&
        instance->objectIndex >= 0 &&
        (uint32_t)instance->objectIndex < runner->dataWin->objt.count) {
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        if (objectName != nullptr && strcmp(objectName, "obj_time") == 0)
            return;
    }
#ifdef NATIVE_GXM_PROBE_VERSION
    /* The GXM renderer cannot yet bind shd_lut's second sampler, so the
     * obj_lutshader event runs a full application_surface GPU/CPU roundtrip
     * without ever applying the LUT. Especially in Chapter5 this produced
     * 912 sprite_create_from_surface synchronizations and 500 surface
     * retirements. On GXM only, omit the visually neutral, unsupported LUT
     * controller passes. All unrelated Draw, Step, shaders and actors run. */
    if ((g_vitaActiveChapter==3 || g_vitaActiveChapter==5) &&
        (eventType==EVENT_STEP || eventType==EVENT_DRAW) &&
        instance->objectIndex>=0 &&
        (uint32_t)instance->objectIndex<runner->dataWin->objt.count){
        const char*obj=runner->dataWin->objt.objects[instance->objectIndex].name;
        if(obj && strcmp(obj,"obj_lutshader")==0){
            /* Native shd_lut now supports its auxiliary sampler. Keep all
             * original LUT scripts on GXM when the GXP was compiled; retain
             * the old cheap fallback if shader program 14 is unavailable. */
            if(g_vitaActiveChapter!=5 ||
               !runner->renderer ||
               !runner->renderer->vtable->shaderIsCompiled ||
               !runner->renderer->vtable->shaderIsCompiled(runner->renderer,14))
                return;
        }
    }
#endif
    if (g_vitaActiveChapter == 3 &&
        (eventType == EVENT_STEP || eventType == EVENT_DRAW) &&
        instance->objectIndex >= 0 &&
        (uint32_t)instance->objectIndex < runner->dataWin->objt.count) {
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        // GameMaker's LUT object performs a full application-surface copy.
        // The Vita legacy backend cannot apply that shader, so executing the
        // pass only saturates the GPU and drops the couch rooms to 15-20 FPS.
        if (!g_vitaModernGlActive &&
            !(g_vitaExperimentalGlActive && g_vitaShadersEnabled) &&
            objectName != nullptr && strcmp(objectName, "obj_lutshader") == 0)
            return;
        // The congratulations effect creates a very dense set of identical
        // confetti instances. Half of them consume ~18 ms on Vita while being
        // visually indistinguishable at 960x544; keep a stable half-set.
        if (objectName != nullptr && strcmp(objectName, "obj_confetti_overworld") == 0 &&
            (instance->instanceId & 1U) != 0)
            return;
        // room_board_1_sword creates many overlapping decorative trees. Its
        // GPU-safe one-quad submission profile makes their Draws expensive,
        // so keep a stable half-set and omit both work and rendering for the
        // other half. Interactive board actors are unaffected.
        if (objectName != nullptr && strcmp(objectName, "obj_board_tree") == 0 &&
            runner->currentRoom != nullptr &&
            strcmp(runner->currentRoom->name, "room_board_1_sword") == 0 &&
            (eventType == EVENT_STEP || eventType == EVENT_DRAW) &&
            (instance->instanceId & 1U) != 0)
            return;
    }
    if (g_vitaActiveChapter == 5 &&
        eventType == EVENT_STEP && eventSubtype == STEP_NORMAL &&
        instance->objectIndex >= 0 &&
        (uint32_t)instance->objectIndex < runner->dataWin->objt.count &&
        runner->currentRoom != nullptr && runner->currentRoom->name != nullptr &&
        strcmp(runner->currentRoom->name, "room_dw_garden_floradinnencounter") == 0) {
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        if (objectName != nullptr && strcmp(objectName, "obj_grassanim_new") == 0 &&
            (runner->frameCount & 1) != 0) {
            // Keep animation time advancing while avoiding the expensive
            // tilemap_get/tilemap_set scan on the skipped frame. The next
            // 15-Hz update catches the tiles up to the current animation.
            int32_t animId = shget(runner->vmContext->varNameMap, "anim");
            int32_t speedId = shget(runner->vmContext->varNameMap, "anim_speed");
            int32_t lengthId = shget(runner->vmContext->varNameMap, "anim_length");
            if (animId >= 0 && speedId >= 0 && lengthId >= 0) {
                GMLReal anim = RValue_toReal(Instance_getSelfVar(instance, animId));
                GMLReal speed = RValue_toReal(Instance_getSelfVar(instance, speedId));
                GMLReal length = RValue_toReal(Instance_getSelfVar(instance, lengthId));
                anim += speed;
                if (length > 0.0 && anim > length) anim -= length;
                Instance_setSelfVar(instance, animId, RValue_makeReal(anim));
            }
            return;
        }
    }

    if (g_vitaActiveChapter == 5 &&
        eventType == EVENT_DRAW && eventSubtype == DRAW_BEGIN &&
        instance->objectIndex >= 0 &&
        (uint32_t)instance->objectIndex < runner->dataWin->objt.count &&
        runner->currentRoom != nullptr && runner->currentRoom->name != nullptr &&
        strcmp(runner->currentRoom->name, "room_dw_fcastle_flowery") == 0) {
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        if (objectName != nullptr && strcmp(objectName, "obj_castlereflect_manager") == 0 &&
            runner->renderer != nullptr && (runner->frameCount % 3U) != 0U) {
            int32_t surfId = shget(runner->vmContext->varNameMap, "surf");
            if (surfId >= 0) {
                RValue surf = Instance_getSelfVar(instance, surfId);
                if (Renderer_surfaceExists(runner->renderer, (int32_t)RValue_toReal(surf))) {
                    static unsigned int lastReflectionReuseLog = 0;
                    if (runner->frameCount - lastReflectionReuseLog >= 120U) {
                        VitaProbe_logLine("CH5_CASTLEREFLECT_REUSE=10hz backend=all");
                        lastReflectionReuseLog = runner->frameCount;
                    }
                    return;
                }
            }
        }
    }

    if (g_vitaActiveChapter == 5 && eventType == EVENT_DRAW && eventSubtype == 0 &&
        instance->objectIndex >= 0 &&
        (uint32_t)instance->objectIndex < runner->dataWin->objt.count) {
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        if (objectName != nullptr) {
            if (!g_vitaModernGlActive && strcmp(objectName, "obj_lutshader") == 0)
                return;
            // The original pass is expensive and was previously suppressed in
            // every Legacy-GL run.  The compact shd_shadowblend programs now
            // make it valid when the user explicitly enables shaders; keeping
            // it suppressed while OFF preserves the low-cost fixed pipeline.
            if (!g_vitaModernGlActive && !g_vitaShadersEnabled &&
                strcmp(objectName, "obj_sunshadows") == 0)
                return;
        }
    }
#endif

    VMContext* vm = runner->vmContext;
    int32_t savedEventType = vm->currentEventType;
    int32_t savedEventSubtype = vm->currentEventSubtype;
    int32_t savedEventObjectIndex = vm->currentEventObjectIndex;
    bool savedActionRelativeFlag = vm->actionRelativeFlag;
    Instance* savedBoardScreenOwner = vm->boardScreenOwner;
    bool savedBoardScreenReuse = vm->boardScreenReuse;
    vm->boardScreenOwner = nullptr;
    vm->boardScreenReuse = false;
    // Keep this opt-in on desktop so local replay exercises the Vita path.
#ifdef PLATFORM_VITA
    bool boardCacheEnabled = g_vitaActiveChapter == 3;
#else
    bool boardCacheEnabled = getenv("BS_TEST_BOARD_SCREEN_CACHE") != nullptr;
#endif
    if (boardCacheEnabled && eventType == EVENT_DRAW && eventSubtype == DRAW_NORMAL &&
        runner->currentRoom && runner->currentRoom->name &&
        strcmp(runner->currentRoom->name, "room_dw_puzzlecloset_2") == 0 &&
        instance->objectIndex >= 0 && (uint32_t)instance->objectIndex < runner->dataWin->objt.count &&
        strcmp(runner->dataWin->objt.objects[instance->objectIndex].name, "obj_board_screen") == 0)
        vm->boardScreenOwner = instance;

    vm->actionRelativeFlag = false;
    vm->currentEventType = eventType;
    vm->currentEventSubtype = eventSubtype;
    vm->currentEventObjectIndex = ownerObjectIndex;

#ifdef ENABLE_VM_TRACING
    if (codeId >= 0 && shlen(vm->eventsToBeTraced) != -1) {
        const char* eventName = Runner_getEventName(eventType, eventSubtype);
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;

        bool shouldTrace = shgeti(vm->eventsToBeTraced, "*") != -1 || shgeti(vm->eventsToBeTraced, eventName) != -1 || shgeti(vm->eventsToBeTraced, objectName) != -1;

        if (shouldTrace) {
            if (eventType == EVENT_ALARM) {
                fprintf(stderr, "Runner: [%s] %s %d (instanceId=%d)\n", objectName, eventName, eventSubtype, instance->instanceId);
            } else {
                fprintf(stderr, "Runner: [%s] %s (instanceId=%d)\n", objectName, eventName, instance->instanceId);
            }
        }
    }
#endif

#ifdef NATIVE_GXM_PROBE_VERSION
    /* Profiling every event adds multiple clock reads per VM call. Sample
     * detailed object timings every eighth frame; script execution,
     * camera and GameMaker ordering remain untouched. */
    const bool gxmSampleEventTiming = (runner->frameCount & 7U) == 0U;
    uint64_t ev_start = gxmSampleEventTiming ? nowNanos() : 0;
#else
    uint64_t ev_start = nowNanos();
#endif
#if IS_WAD17_OR_HIGHER_ENABLED
    bool nativeDraw = eventType == EVENT_DRAW && eventSubtype == DRAW_NORMAL &&
                      (VMBuiltins_tryCullTVPanel(vm, instance, codeId) ||
                       VMBuiltins_tryGardenLightDraw(vm, instance, codeId));
#ifdef PLATFORM_VITA
    if (!nativeDraw && g_vitaActiveChapter == 5 && eventType == EVENT_DRAW &&
        eventSubtype == DRAW_NORMAL)
        nativeDraw = VMBuiltins_tryManeDraw(vm, instance, codeId);
#endif
    if (!nativeDraw) executeCode(runner, instance, codeId);
#else
    executeCode(runner, instance, codeId);
#endif
#ifdef NATIVE_GXM_PROBE_VERSION
    extern int g_vitaActiveChapter;
    /* Chapter0 lives on ux0: as an external GameMaker data.win. Older
     * installed versions and translation overrides may retain v0.71,
     * even when VPK and prepared PC data.win are v0.78. Fix the actual
     * displayed GameMaker variable immediately after Create, without
     * mutating/redistributing the user's original data.win. */
    if(g_vitaActiveChapter==0 && eventType==EVENT_CREATE &&
       instance->objectIndex>=0 &&
       (uint32_t)instance->objectIndex<runner->dataWin->objt.count){
        const char*name=runner->dataWin->objt.objects[instance->objectIndex].name;
        if(name && strcmp(name,"obj_ui_version")==0){
            int32_t vid=VM_getOrAllocateVarID(vm,"_version_text");
            if(vid>=0)Instance_setSelfVar(instance,vid,
                RValue_makeOwnedString(safeStrdup("DELTARUNE VITA v0.78")));
        }
    }
#endif
#ifdef PLATFORM_VITA
    vitaEnableChapter5CliffSimpleMode(runner, instance, eventType);
#endif
#if defined(PLATFORM_VITA) || defined(_WIN32)
    if (eventType == EVENT_STEP && eventSubtype == STEP_NORMAL
#ifndef PLATFORM_VITA
        && getenv("BS_TEST_RETRY") != nullptr
#endif
    ) Runner_shortenRetryFade(runner, instance);
#endif
#ifdef NATIVE_GXM_PROBE_VERSION
    uint64_t ev_end = gxmSampleEventTiming ? nowNanos() : 0;
    uint64_t dur = gxmSampleEventTiming ? (ev_end-ev_start)/1000 : 0;
#else
    uint64_t ev_end = nowNanos();
    uint64_t dur = (ev_end-ev_start)/1000;
#endif
#ifdef NATIVE_GXM_PROBE_VERSION
    /* Record Begin, Normal AND End Step. Previous measurements included
     * all three phases in events_us, but step_hotspot counted only Normal:
     * it misleadingly attributed 0.6 ms of the full 23 ms VM cost.
     * Composite key preserves object and Step phase; no disk I/O. */
    if(gxmSampleEventTiming && eventType==EVENT_STEP &&
       (eventSubtype==STEP_BEGIN||eventSubtype==STEP_NORMAL||
        eventSubtype==STEP_END) && instance->objectIndex>=0){
        unsigned phase=eventSubtype==STEP_BEGIN?0u:
                       eventSubtype==STEP_NORMAL?1u:2u;
        runner->gxmStepPhaseUs[phase]+=dur*8u;
        runner->gxmStepPhaseCalls[phase]+=8u;
        uint32_t key=(((uint32_t)instance->objectIndex+1u)<<2)|phase;
        uint32_t slot=key&127u;
        for(unsigned probe=0;probe<128;probe++,slot=(slot+1u)&127u){
            if(runner->gxmStepProfileKeys[slot]==0||
               runner->gxmStepProfileKeys[slot]==key){
                runner->gxmStepProfileKeys[slot]=key;
                runner->gxmStepProfileUs[slot]+=dur*8u;
                runner->gxmStepProfileCalls[slot]+=8u;
                break;
            }
        }
    }
#endif
    if (eventType == EVENT_DRAW && dur > runner->profDrawMaxUs) {
        runner->profDrawMaxUs = dur;
        runner->profDrawMaxObject = instance->objectIndex;
    }
    if (dur >= 2500 && (runner->profSlowEventLastLogNs == 0 ||
                       ev_end - runner->profSlowEventLastLogNs >= 1000000000ULL)) {
        const char* eventName = Runner_getEventName(eventType, eventSubtype);
        const char* objectName = runner->dataWin->objt.objects[instance->objectIndex].name;
        // Rhythm-game Draw exceeds 2.5 ms every frame. Logging every event
        // adds synchronous I/O to the hot path. Keep per-frame maxima above,
        // but emit at most one detailed event per second, with a skip count.
        printf("PERF_EVENT_SLOW obj=%s inst=%d evt=%s sub=%d dur_us=%llu suppressed=%u\n", objectName, instance->instanceId, eventName, (int)eventSubtype, (unsigned long long)dur, runner->profSlowEventsSuppressed);
        runner->profSlowEventLastLogNs = ev_end;
        runner->profSlowEventsSuppressed = 0;
    } else if (dur >= 2500) {
        runner->profSlowEventsSuppressed++;
    }
    vm->currentEventType = savedEventType;
    vm->currentEventSubtype = savedEventSubtype;
    vm->currentEventObjectIndex = savedEventObjectIndex;
    vm->actionRelativeFlag = savedActionRelativeFlag;
    vm->boardScreenOwner = savedBoardScreenOwner;
    vm->boardScreenReuse = savedBoardScreenReuse;
}

void Runner_executeEventFromObject(Runner* runner, Instance* instance, int32_t startObjectIndex, int32_t eventType, int32_t eventSubtype) {
    int32_t ownerObjectIndex = -1;
    int32_t codeId = findEventCodeIdAndOwner(runner, startObjectIndex, eventType, eventSubtype, &ownerObjectIndex);
    // Fast path: If the codeId is invalid, let's bail out fast
    // This way can avoid the need of loading and saving the current state variables
    if (0 > codeId)
        return;
    Runner_executeResolvedEvent(runner, instance, eventType, eventSubtype, codeId, ownerObjectIndex);
}

#ifdef PLATFORM_VITA
static void vitaAdjustChapter2FixedBattleBubble(Runner* runner, Instance* instance,
                                                 int32_t eventType, int32_t eventSubtype) {
    if (runner == nullptr || instance == nullptr || runner->vmContext == nullptr ||
        runner->vitaBattleBubbleObjectIndex < 0 ||
        instance->objectIndex != runner->vitaBattleBubbleObjectIndex ||
        eventType != EVENT_DRAW || eventSubtype != DRAW_NORMAL)
        return;

    VMContext* vm = runner->vmContext;
    if (vm->globalScopeInstance == nullptr) return;

    int32_t chapterVar = VM_getOrAllocateVarID(vm, "chapter");
    RValue chapterValue = Instance_getSelfVar(vm->globalScopeInstance, chapterVar);
    if (chapterValue.type == RVALUE_UNDEFINED || RValue_toInt32(chapterValue) != 2) return;

    int32_t autoLengthVar = VM_getOrAllocateVarID(vm, "auto_length");
    int32_t initVar = VM_getOrAllocateVarID(vm, "init");
    RValue autoLength = Instance_getSelfVar(instance, autoLengthVar);
    RValue init = Instance_getSelfVar(instance, initVar);
    if (autoLength.type == RVALUE_UNDEFINED || init.type == RVALUE_UNDEFINED ||
        RValue_toInt32(autoLength) != 0 || RValue_toInt32(init) != 0)
        return;

    int32_t myWriterVar = VM_getOrAllocateVarID(vm, "mywriter");
    RValue writerRef = Instance_getSelfVar(instance, myWriterVar);
    if (writerRef.type == RVALUE_UNDEFINED) return;
    int32_t writerId = RValue_toInt32(writerRef);
    Instance* writer = hmget(runner->instancesById, writerId);
    if (writer == nullptr || writer->destroyed) return;

    // Auto-length battle balloons already receive a 4 px upward text offset in
    // the Chapter 2 GML patch. Fixed enemy balloons skip that branch entirely;
    // mirror the same offset once, immediately before their first Draw event.
    const GMLReal offset = 4.0;
    int32_t writingYVar = VM_getOrAllocateVarID(vm, "writingy");
    RValue writingY = Instance_getSelfVar(writer, writingYVar);
    GMLReal adjustedWritingY = (writingY.type == RVALUE_UNDEFINED)
        ? (GMLReal)writer->y - offset
        : RValue_toReal(writingY) - offset;
    writer->y -= (float)offset;
    Instance_setSelfVar(writer, writingYVar, RValue_makeReal(adjustedWritingY));
}
#endif

void Runner_executeEvent(Runner* runner, Instance* instance, int32_t eventType, int32_t eventSubtype) {
#ifdef PLATFORM_VITA
    vitaAdjustChapter2FixedBattleBubble(runner, instance, eventType, eventSubtype);
#endif
    Runner_executeEventFromObject(runner, instance, instance->objectIndex, eventType, eventSubtype);
}

// Events that GameMaker routes through the per-object obj_has_event table instead of Perform_Event_All.
static bool eventUsesPerObjectDispatch(int32_t eventType) {
    return eventType == EVENT_STEP || eventType == EVENT_ALARM || eventType == EVENT_KEYBOARD || eventType == EVENT_KEYPRESS || eventType == EVENT_KEYRELEASE;
}

void Runner_executeEventForAll(Runner* runner, int32_t eventType, int32_t eventSubtype) {
    int32_t slot = EventSlotMap_lookup(&runner->eventSlotMap, eventType, eventSubtype);
    if (slot == -1) return;

    // We always snapshot the iteration list before dispatching so instances spawned during this phase do NOT fire the current event.
    Instance** scratch = runner->eventDispatchInstances;
    arrsetlen(scratch, 0);

    if (eventUsesPerObjectDispatch(eventType)) {
        ResolvedEventTable* table = &runner->eventTable;
        uint32_t entryCount;
        SlotResponderEntry* entries = ResolvedEventTable_slotEntries(table, slot, &entryCount);
        if (entryCount == 0) return;

        repeat(entryCount, i) {
            int32_t concreteObj = entries[i].concreteObjectId;
            Instance** bucket = runner->instancesByExactObject[concreteObj];
            int32_t bucketCount = (int32_t) arrlen(bucket);
            if (bucketCount == 0) continue;
            size_t base = arrlenu(scratch);
            arrsetlen(scratch, base + (size_t) bucketCount);
            memcpy(&scratch[base], bucket, (size_t) bucketCount * sizeof(Instance*));
        }
        runner->eventDispatchInstances = scratch; // arrsetlen may have realloced

        int32_t snapshotCount = (int32_t) arrlen(scratch);
#ifdef NATIVE_GXM_PROBE_VERSION
        /* Step snapshots are grouped by exact object. Resolve the inherited
         * event code once for the bucket instead of resolving the same
         * event slot + parent hierarchy for every one of its instances.
         * The bucket snapshot and original event order are untouched.
         * If instance_change happens during dispatch, objectIndex differs
         * and we immediately resolve its new object instead. */
        int32_t cachedObject = -2,cachedCode = -1,cachedOwner = -1;
#endif
        repeat(snapshotCount, i) {
            Instance* inst = scratch[i];
            if (!inst->active) continue;
#ifdef NATIVE_GXM_PROBE_VERSION
            if(eventType==EVENT_STEP){
                int32_t obj=inst->objectIndex;
                if(obj!=cachedObject){
                    cachedObject=obj;
                    cachedCode=ResolvedEventTable_lookup(
                        table,obj,slot,&cachedOwner);
                }else{
                    runner->gxmStepLookupSaved++;
                }
                if(cachedCode>=0)Runner_executeResolvedEvent(
                     runner,inst,eventType,eventSubtype,cachedCode,cachedOwner);
                continue;
            }
#endif
            Runner_executeEvent(runner, inst, eventType, eventSubtype);
        }
        return;
    }

    int32_t count = (int32_t) arrlen(runner->instances);
    if (count == 0) return;
    arrsetlen(scratch, count);
    memcpy(scratch, runner->instances, (size_t) count * sizeof(Instance*));
    runner->eventDispatchInstances = scratch;

    repeat(count, i) {
        Instance* inst = scratch[i];
        if (!inst->active) continue;
        // Skip non-responders without entering Runner_executeEvent. ResolvedEventTable_lookup is a tiny CSR scan; non-responders bail in a few compares and avoid the VM state save/restore overhead inside Runner_executeEventFromObject.
        int32_t ownerObjectIndex = -1;
        int32_t codeId = ResolvedEventTable_lookup(&runner->eventTable, inst->objectIndex, slot, &ownerObjectIndex);
        if (0 > codeId) continue;
        Runner_executeResolvedEvent(runner, inst, eventType, eventSubtype, codeId, ownerObjectIndex);
    }
}

void Runner_setLives(Runner* runner, GMLReal value) {
    GMLReal old = runner->lives;
    runner->lives = value;
    if (old > 0.0 && 0.0 >= value) Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_NO_MORE_LIVES);
}

void Runner_setHealth(Runner* runner, GMLReal value) {
    GMLReal old = runner->health;
    runner->health = value;
    if (old > 0.0 && 0.0 >= value) Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_NO_MORE_HEALTH);
}

// ===[ Background Scrolling & Drawing ]===

void Runner_scrollBackgrounds(Runner* runner) {
    repeat(8, i) {
        RuntimeBackground* bg = &runner->backgrounds[i];
        if (!bg->visible) continue;
        bg->x += bg->speedX;
        bg->y += bg->speedY;
    }
}

static void drawBackground(
    Runner* runner,
    int32_t tpagIndex,
    bool stretch,
    float roomW,
    float roomH,
    bool tileX,
    bool tileY,
    float backgroundX,
    float backgroundY,
    float layerOffsetX,
    float layerOffsetY,
    float xScale,
    float yScale,
    uint32_t blend,
    float alpha
) {
    if (0 > tpagIndex)
        return;

    if (stretch) {
        // Stretch to fill room dimensions
        TexturePageItem* tpag = &runner->dataWin->tpag.items[tpagIndex];
        float xscale = roomW / (float) tpag->boundingWidth;
        float yscale = roomH / (float) tpag->boundingHeight;
        runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, 0.0f, 0.0f, 0.0f, 0.0f, xscale, yscale, 0.0f, blend, alpha);
    } else if (tileX || tileY) {
        Renderer_drawBackgroundTiled(runner->renderer, tpagIndex, layerOffsetX + backgroundX, layerOffsetY + backgroundY, xScale, yScale, tileX, tileY, roomW, roomH, alpha);
    } else {
        // Single placement
        runner->renderer->vtable->drawSprite(runner->renderer, tpagIndex, layerOffsetX + backgroundX, layerOffsetY + backgroundY, 0.0f, 0.0f, xScale, yScale, 0.0f, blend, alpha);
    }
}

// Renders a single GM:S 2 background layer element
static void renderBackgroundElement(Runner* runner, RuntimeBackgroundElement* bg, float roomW, float roomH, float layerOffsetX, float layerOffsetY) {
    if (bg == nullptr || !bg->visible) return;
    if (0 > bg->spriteIndex) {
        // Spriteless background element: draw as a colored rectangle filling the room
        // TODO: Match GMS's rendering more closely (see PR #120)
        runner->renderer->vtable->drawRectangle(runner->renderer, 0.0f, 0.0f, roomW, roomH, bg->blend, bg->alpha, false);
        return;
    }
    int32_t tpagIndex = Renderer_resolveTPAGIndex(runner->dataWin, bg->spriteIndex, bg->imageIndex);
    drawBackground(runner, tpagIndex, bg->stretch, roomW, roomH, bg->hTiled, bg->vTiled, bg->xOffset, bg->yOffset, layerOffsetX, layerOffsetY, bg->xScale, bg->yScale, bg->blend, bg->alpha);
}

// Legacy GameMaker: Studio 1.x backgrounds
static void drawGMS1Backgrounds(Runner* runner, bool foreground) {
    if (runner->renderer == nullptr) return;
    DataWin* dataWin = runner->dataWin;
    float roomW = (float) runner->currentRoom->width;
    float roomH = (float) runner->currentRoom->height;

    repeat(8, i) {
        RuntimeBackground* bg = &runner->backgrounds[i];
        if (!bg->visible || bg->foreground != foreground) continue;
        if (0 > bg->backgroundIndex) continue;

        int32_t tpagIndex = Renderer_resolveBackgroundTPAGIndex(dataWin, bg->backgroundIndex);

        drawBackground(
            runner,
            tpagIndex,
            bg->stretch,
            roomW,
            roomH,
            bg->tileX,
            bg->tileY,
            bg->x,
            bg->y,
            0.0f,
            0.0f,
            bg->xScale,
            bg->yScale,
            0xFFFFFF,
            bg->alpha
        );
    }
}

// ===[ Draw ]===

typedef struct {
    int32_t depth;
    int32_t type;
    int32_t order;
} DrawKey;

static DrawKey drawableKey(const Drawable* d) {
    DrawKey k = { d->depth, d->type, 0 };
    switch (d->type) {
        case DRAWABLE_TILE: k.order = d->tileIndex; break;
        case DRAWABLE_INSTANCE: k.order = (int32_t) d->instance->instanceId;  break;
        case DRAWABLE_LAYER: k.order = d->runtimeLayerId; break;
    }
    return k;
}

static int compareDrawKeys(const DrawKey* a, const DrawKey* b) {
    if (a->depth != b->depth)
        return a->depth > b->depth ? -1 : 1; // higher depth first

    if (a->type  != b->type)
        return a->type  < b->type  ? -1 : 1; // tiles before instances

    if (a->type == DRAWABLE_TILE)
        return (a->order > b->order) - (a->order < b->order); // tiles: higher index later

    return (a->order < b->order) - (a->order > b->order); // instance/layer: higher first
}

static int compareDrawables(const void* a, const void* b) {
    Drawable* drawableA = (Drawable*) a;
    Drawable* drawableB = (Drawable*) b;
    DrawKey drawKeyA = drawableKey(drawableA);
    DrawKey drawKeyB = drawableKey(drawableB);

    return compareDrawKeys(&drawKeyA, &drawKeyB);
}

static void fireDrawSubtype(Runner* runner, Drawable* drawables, int32_t drawableCount, int32_t subtype) {
    int32_t slot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_DRAW, subtype);
    if (slot == -1) return;

    repeat(drawableCount, i) {
        Drawable* d = &drawables[i];
        if (d->type != DRAWABLE_INSTANCE)
            continue;

        Instance* inst = d->instance;
        if (!inst->active || !inst->visible)
            continue;

        int32_t ownerObjectIndex = -1;
        int32_t codeId = ResolvedEventTable_lookup(&runner->eventTable, inst->objectIndex, slot, &ownerObjectIndex);
        if (0 > codeId) continue;
        Runner_executeResolvedEvent(runner, inst, EVENT_DRAW, subtype, codeId, ownerObjectIndex);
    }
}

// GMS2 tilemap cell bit layout (matches HTML5 Function_Layers.js TileIndex/Mirror/Flip/Rotate masks)
#define GMS2_TILE_INDEX_MASK  0x0007FFFF // bits 0..18
#define GMS2_TILE_MIRROR_MASK 0x10000000 // bit 28 (horizontal flip)
#define GMS2_TILE_FLIP_MASK   0x20000000 // bit 29 (vertical flip)
#define GMS2_TILE_ROTATE_MASK 0x40000000 // bit 30 (90 CW)

uint32_t Runner_tileAnimationFrame(const Background* tileset, uint64_t timeUs, uint32_t previousFrame) {
    uint32_t frames = tileset->gms2ItemsPerTileCount;
    if (frames <= 1) return 0;
    if (tileset->gms2FrameLength > 0)
        return (uint32_t)((timeUs / (uint64_t)tileset->gms2FrameLength) % frames);
    return (previousFrame + 1) % frames;
}

uint32_t Runner_tileFrameIndex(const Background* tileset, uint32_t tileIndex, int32_t frame) {
    if (tileIndex == 0 || tileIndex >= tileset->gms2TileCount) return 0;
    uint32_t frames = tileset->gms2ItemsPerTileCount;
    if (!frames || tileset->gms2TileIds == nullptr) return tileIndex;
    int64_t wrapped = (int64_t)frame % frames;
    if (wrapped < 0) wrapped += frames;
    return tileset->gms2TileIds[(size_t)tileIndex * frames + wrapped];
}

void Runner_drawTileLayer(Runner* runner, RoomLayerTilesData* data, float layerOffsetX, float layerOffsetY, uint32_t frame) {
    if (data == nullptr || data->tileData == nullptr) return;
    if (0 > data->backgroundIndex) return;

    DataWin* dw = runner->dataWin;
    if ((uint32_t) data->backgroundIndex >= dw->bgnd.count) return;

    Background* tileset = &dw->bgnd.backgrounds[data->backgroundIndex];
    if (tileset->gms2TileWidth == 0 || tileset->gms2TileHeight == 0 || tileset->gms2TileColumns == 0) return;

    int32_t tpagIndex = tileset->tpagIndex;
    if (0 > tpagIndex) return;

    uint32_t tileW = tileset->gms2TileWidth;
    uint32_t tileH = tileset->gms2TileHeight;
    uint32_t borderX = tileset->gms2OutputBorderX;
    uint32_t borderY = tileset->gms2OutputBorderY;
    uint32_t columns = tileset->gms2TileColumns;

    static bool rotateWarned = false;

    // Large GMS2 rooms can contain tens of thousands of cells.  Drawing every
    // non-empty cell is particularly expensive on Vita because each tile is a
    // separate legacy GL submission.  Limit the walk to the active camera plus
    // a two-tile guard band.  Rotated cameras keep the conservative full-layer
    // path because their axis-aligned bounds need different handling.
    int32_t firstTx = 0;
    int32_t firstTy = 0;
    int32_t lastTx = (int32_t) data->tilesX;
    int32_t lastTy = (int32_t) data->tilesY;
    GMLCamera* camera = Runner_getCameraById(runner, runner->renderer->cameraCurrent);
    if (camera != nullptr && camera->viewAngle == 0.0f && tileW > 0 && tileH > 0) {
        const int32_t guard = 2;
        firstTx = (int32_t) floorf((camera->viewX - layerOffsetX) / (float) tileW) - guard;
        firstTy = (int32_t) floorf((camera->viewY - layerOffsetY) / (float) tileH) - guard;
        lastTx = (int32_t) ceilf((camera->viewX + (float) camera->viewWidth - layerOffsetX) / (float) tileW) + guard;
        lastTy = (int32_t) ceilf((camera->viewY + (float) camera->viewHeight - layerOffsetY) / (float) tileH) + guard;
        if (firstTx < 0) firstTx = 0;
        if (firstTy < 0) firstTy = 0;
        if (lastTx > (int32_t) data->tilesX) lastTx = (int32_t) data->tilesX;
        if (lastTy > (int32_t) data->tilesY) lastTy = (int32_t) data->tilesY;
    }

    for (int32_t ty = firstTy; ty < lastTy; ty++) {
        for (int32_t tx = firstTx; tx < lastTx; tx++) {
            uint32_t cell = data->tileData[ty * data->tilesX + tx];
            uint32_t tileIndex = Runner_tileFrameIndex(tileset, cell & GMS2_TILE_INDEX_MASK, (int32_t)frame);
            if (tileIndex == 0) continue; // 0 = empty

            uint32_t col = tileIndex % columns;
            uint32_t row = tileIndex / columns;
            int32_t srcX = (int32_t) (col * (tileW + 2 * borderX) + borderX);
            int32_t srcY = (int32_t) (row * (tileH + 2 * borderY) + borderY);

            bool mirror = (cell & GMS2_TILE_MIRROR_MASK) != 0;
            bool flip = (cell & GMS2_TILE_FLIP_MASK) != 0;
            bool rotate = (cell & GMS2_TILE_ROTATE_MASK) != 0;

            if (rotate && !rotateWarned) {
                fprintf(stderr, "Runner: WARNING: GMS2 tile layer has rotated tiles; rotation not yet implemented, drawing unrotated\n");
                rotateWarned = true;
            }

            float xscale = mirror ? -1.0f : 1.0f;
            float yscale = flip ? -1.0f : 1.0f;

            // With negative scale the quad grows in the opposite direction, so shift the
            // destination by one tile to keep the origin at the top-left of the cell.
            float dstX = (float) (tx * tileW) + layerOffsetX + (mirror ? (float) tileW : 0.0f);
            float dstY = (float) (ty * tileH) + layerOffsetY + (flip ? (float) tileH : 0.0f);

            runner->renderer->vtable->drawSpritePart(runner->renderer, tpagIndex, srcX, srcY, (int32_t) tileW, (int32_t) tileH, dstX, dstY, xscale, yscale, 0.0f, 0.0f, 0.0f, 0xFFFFFF, 1.0f);
        }
    }
}

// Returns true if "drawables" is already in compareDrawableDepth order. Used by the sort-dirty path to skip qsort when small depth perturbations didn't actually cross any neighbor.
static bool isDrawableArraySorted(Drawable* drawables, int32_t count) {
    for (int32_t i = 1; count > i; i++) {
        DrawKey drawKey1 = drawableKey(&drawables[i - 1]);
        DrawKey drawKey2 = drawableKey(&drawables[i]);

        if (compareDrawKeys(&drawKey1, &drawKey2) > 0) return false;
    }
    return true;
}

// Refreshes each entry's cached .depth from the live instance/runtime-layer pointer. Tile entries never change depth mid-room so they're left alone.
static void refreshDrawableDepths(Runner* runner, Drawable* drawables, int32_t count) {
    for (int32_t i = 0; count > i; i++) {
        Drawable* d = &drawables[i];
        if (d->type == DRAWABLE_INSTANCE) {
            d->depth = d->instance->depth;
        } else if (d->type == DRAWABLE_LAYER) {
            RuntimeLayer* rl = Runner_findRuntimeLayerById(runner, d->runtimeLayerId);
            if (rl != nullptr) d->depth = rl->depth;
        }
    }
}

// Rebuilds runner->cachedDrawables when invalidated. Two-tier strategy:
//   structureDirty - the SET of entries changed (instance/layer create or destroy, room change). Drop the cache and re-add every instance/tile/runtime-layer, then qsort.
//   sortDirty only - the entries are the same but .depth values may have shifted. Refresh depths from the live sources and only qsort if the order actually broke.
static void rebuildDrawableCacheIfDirty(Runner* runner) {
    if (runner->drawableListStructureDirty) {
        arrsetlen(runner->cachedDrawables, 0);
        Room* room = runner->currentRoom;
        if (room == nullptr) {
            runner->drawableListStructureDirty = false;
            runner->drawableListSortDirty = false;
            return;
        }

        int32_t instanceCount = (int32_t) arrlen(runner->instances);
        repeat(instanceCount, i) {
            Instance* inst = runner->instances[i];
            Drawable d;
            ZERO_STRUCT(d);
            d.type = DRAWABLE_INSTANCE;
            d.depth = inst->depth;
            d.instance = inst;
            arrput(runner->cachedDrawables, d);
        }

        if (!DataWin_isVersionAtLeast(runner->dataWin, 2, 0, 0, 0)) {
            repeat(room->tileCount, i) {
                RoomTile* tile = &room->tiles[i];
                Drawable d;
                ZERO_STRUCT(d);
                d.type = DRAWABLE_TILE;
                d.depth = tile->tileDepth;
                d.tileIndex = (int32_t) i;
                arrput(runner->cachedDrawables, d);
            }
        } else {
            size_t runtimeLayersCount = arrlenu(runner->runtimeLayers);
            repeat(runtimeLayersCount, i) {
                RuntimeLayer* runtimeLayer = &runner->runtimeLayers[i];
                Drawable d;
                ZERO_STRUCT(d);
                d.type = DRAWABLE_LAYER;
                d.depth = runtimeLayer->depth;
                d.runtimeLayerId = (int32_t) runtimeLayer->id;
                arrput(runner->cachedDrawables, d);
            }
        }

        int32_t count = (int32_t) arrlen(runner->cachedDrawables);
        if (count > 1) {
            qsort(runner->cachedDrawables, count, sizeof(Drawable), compareDrawables);
        }
        runner->drawableListStructureDirty = false;
        runner->drawableListSortDirty = false;
        return;
    }

    if (runner->drawableListSortDirty) {
        int32_t count = (int32_t) arrlen(runner->cachedDrawables);
        refreshDrawableDepths(runner, runner->cachedDrawables, count);
        if (count > 1 && !isDrawableArraySorted(runner->cachedDrawables, count)) {
            qsort(runner->cachedDrawables, count, sizeof(Drawable), compareDrawables);
        }
        runner->drawableListSortDirty = false;
    }
}

void Runner_draw(Runner* runner) {
    Room* room = runner->currentRoom;

#ifdef PLATFORM_VITA
    // DELTARUNE battles are overlays and leave the Cyber room parallax active
    // underneath. On Vita that visually covered work costs about 4.6 ms per
    // frame. Detect the real battle controller, then omit only the deepest
    // decorative parallax layer; gameplay, bullets and the battle background
    // continue drawing normally.
    bool vitaCh2BattleOverlay = false;
    extern int g_vitaActiveChapter;
    if (g_vitaActiveChapter == 2) {
        for (int32_t bi = 0; bi < (int32_t)arrlen(runner->instances); ++bi) {
            Instance* candidate = runner->instances[bi];
            if (!candidate->active || candidate->objectIndex < 0 ||
                (uint32_t)candidate->objectIndex >= runner->dataWin->objt.count) continue;
            const char* candidateName = runner->dataWin->objt.objects[candidate->objectIndex].name;
            if (candidateName != nullptr && strcmp(candidateName, "obj_battlecontroller") == 0) {
                vitaCh2BattleOverlay = true;
                break;
            }
        }
    }
#endif

    rebuildDrawableCacheIfDirty(runner);
    int32_t drawableCount = (int32_t) arrlen(runner->cachedDrawables);

    // Draw non-foreground backgrounds (behind everything)
    if (!DataWin_isVersionAtLeast(runner->dataWin, 2, 0, 0, 0))
        drawGMS1Backgrounds(runner, false);

    fireDrawSubtype(runner, runner->cachedDrawables, drawableCount, DRAW_BEGIN);

    // Draw interleaved tiles and instances
    int32_t i = 0;
    DrawKey lastProcessedDrawKey;

    while (true) {
        if (runner->drawableListSortDirty || runner->drawableListStructureDirty) {
            rebuildDrawableCacheIfDirty(runner);

            if (i != 0) {
                // Something created things during draw events! Figure out the new cursor position...
                // lastProcessedDrawKey WILL already be set here, because this code will ONLY execute if we have at least processed at least ONE drawable
                bool foundSomething = false;

                repeat(arrlen(runner->cachedDrawables), j) {
                    Drawable* drawable = &runner->cachedDrawables[j];

                    DrawKey drawKey = drawableKey(drawable);

                    int result = compareDrawKeys(&drawKey, &lastProcessedDrawKey);

                    if (result == 0) {
                        // Found exact match
                        i = (int32_t) j + 1; // Skip one because we DON'T WANT to redraw something that was already drawn
                        foundSomething = true;
                        break;
                    }

                    if (result == 1) {
                        // New key order is HIGHER than the current one, so we want to continue processing from here
                        i = (int32_t) j;
                        foundSomething = true;
                        break;
                    }
                }

                if (!foundSomething) {
                    // We haven't found anything, we don't have anything more to draw, so bail!
                    break;
                }
            }

            drawableCount = arrlen(runner->cachedDrawables);
        }

        if (i >= drawableCount)
            break;

        Drawable* d = &runner->cachedDrawables[i++];
        lastProcessedDrawKey = drawableKey(d);

        if (d->type == DRAWABLE_TILE) {
#ifdef PLATFORM_VITA
            // A DELTARUNE battle draws its own full-screen presentation. Keeping
            // the room tilemap alive underneath it needlessly pulls the Cyber
            // room atlases back into CDRAM and evicts the battle pages. The
            // resulting upload/eviction loop produced multi-second frames.
            if (vitaCh2BattleOverlay) continue;
#endif
            if (runner->renderer != nullptr) {
                RoomTile* tile = &room->tiles[d->tileIndex];
                // Skip tiles whose layer was hidden via tile_layer_hide(). Filtered here (not in the cache) so toggling layer visibility doesn't invalidate.
                ptrdiff_t layerIdx = hmgeti(runner->tileLayerMap, tile->tileDepth);
                if (layerIdx >= 0 && !runner->tileLayerMap[layerIdx].value.visible) continue;
                float offsetX = 0.0f, offsetY = 0.0f;
                if (layerIdx >= 0) {
                    offsetX = runner->tileLayerMap[layerIdx].value.offsetX;
                    offsetY = runner->tileLayerMap[layerIdx].value.offsetY;
                }

#ifdef ENABLE_VM_TRACING
                // Trace tile drawing if requested
                if (shlen(runner->vmContext->tilesToBeTraced) > 0) {
                    DataWin* dataWin = runner->dataWin;
                    const char* bgName = (tile->backgroundDefinition >= 0 && dataWin->bgnd.count > (uint32_t) tile->backgroundDefinition) ? dataWin->bgnd.backgrounds[tile->backgroundDefinition].name : "<none>";
                    const char* roomName = room->name;

                    bool shouldTrace = shgeti(runner->vmContext->tilesToBeTraced, "*") != -1 || shgeti(runner->vmContext->tilesToBeTraced, bgName) != -1 || shgeti(runner->vmContext->tilesToBeTraced, roomName) != -1;

                    if (shouldTrace) {
                        int32_t tpagIndex = Renderer_resolveObjectTPAGIndex(dataWin, tile);
                        if (tpagIndex >= 0) {
                            TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];
                            fprintf(stderr, "Runner: [%s] Drawing tile #%d bg=%s(%d) tpag(srcX=%d srcY=%d srcW=%d srcH=%d tgtX=%d tgtY=%d bndW=%d bndH=%d page=%d) tile(srcX=%d srcY=%d w=%u h=%u) at pos=(%d,%d) depth=%d\n", roomName, d->tileIndex, bgName, tile->backgroundDefinition, tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight, tpag->targetX, tpag->targetY, tpag->boundingWidth, tpag->boundingHeight, tpag->texturePageId, tile->sourceX, tile->sourceY, tile->width, tile->height, tile->x, tile->y, tile->tileDepth);

                            // Warn if tile source rect exceeds TPAG content bounds
                            if ((uint32_t) (tile->sourceX + tile->width) > (uint32_t) tpag->sourceWidth || (uint32_t) (tile->sourceY + tile->height) > (uint32_t) tpag->sourceHeight) {
                                fprintf(stderr, "Runner: [%s] WARNING: Tile #%d source rect (%d,%d %ux%u) exceeds TPAG content bounds (%dx%d)\n", roomName, d->tileIndex, tile->sourceX, tile->sourceY, tile->width, tile->height, tpag->sourceWidth, tpag->sourceHeight);
                            }
                        } else {
                            fprintf(stderr, "Runner: [%s] Drawing tile #%d bg=%s(%d) tpag=UNRESOLVED tile(srcX=%d srcY=%d w=%u h=%u) at pos=(%d,%d) depth=%d\n", roomName, d->tileIndex, bgName, tile->backgroundDefinition, tile->sourceX, tile->sourceY, tile->width, tile->height, tile->x, tile->y, tile->tileDepth);
                        }
                    }
                }
#endif

                Renderer_drawTile(runner->renderer, tile, offsetX, offsetY);
            }
        } else if (d->type == DRAWABLE_INSTANCE) {
            Instance* inst = d->instance;
            // Filter inactive/invisible instances at draw time so the cache doesn't need invalidation when those flags toggle.
            if (!inst->active || !inst->visible) continue;
#ifdef PLATFORM_VITA
            if (inst->objectIndex >= 0 &&
                (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                const char* drawObjectName = runner->dataWin->objt.objects[inst->objectIndex].name;
                extern void VitaBorders_noteTransitionObject(const char* objectName);
                VitaBorders_noteTransitionObject(drawObjectName);
            }
            if (g_vitaActiveChapter == 3 && inst->objectIndex >= 0 &&
                (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                extern void VitaBorders_noteVisibleAsset(const char* assetName);
                VitaBorders_noteVisibleAsset(
                    runner->dataWin->objt.objects[inst->objectIndex].name);
                if (inst->spriteIndex >= 0 &&
                    (uint32_t)inst->spriteIndex < runner->dataWin->sprt.count)
                    VitaBorders_noteVisibleAsset(
                        runner->dataWin->sprt.sprites[inst->spriteIndex].name);
            }
            if (vitaCh2BattleOverlay && inst->objectIndex >= 0 &&
                (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                const char* drawObjectName = runner->dataWin->objt.objects[inst->objectIndex].name;
                if (vitaIsCh2ExplorationDrawObject(drawObjectName))
                    continue;
            }
            if (g_vitaActiveChapter == 2 && inst->objectIndex >= 0 &&
                (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                const char* drawObjectName = runner->dataWin->objt.objects[inst->objectIndex].name;
                if (vitaCullCh2MusicBulletDraw(runner, inst, drawObjectName))
                    continue;
            }
#endif
            int32_t ownerObjectIndex = -1;
            int32_t codeId = findEventCodeIdAndOwner(runner, inst->objectIndex, EVENT_DRAW, DRAW_NORMAL, &ownerObjectIndex);
            if (codeId >= 0) {
                Runner_executeResolvedEvent(runner, inst, EVENT_DRAW, DRAW_NORMAL, codeId, ownerObjectIndex);
            } else if (runner->renderer != nullptr) {
                Renderer_drawSelf(runner->renderer, inst);
            }
        } else if (d->type == DRAWABLE_LAYER) {
            // Re-resolve every iteration: a previous instance's Draw event may have called layer_create/layer_destroy and reallocated runner->runtimeLayers.
            RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, d->runtimeLayerId);
            if (runtimeLayer == nullptr || !runtimeLayer->visible) continue;
#ifdef PLATFORM_VITA
            // Static Room layers are fully covered by the battle overlay. Do
            // not render their backgrounds, tiles or placed sprites while the
            // controller is active. Instance draw events remain untouched, so
            // the battle controller, fighters, bullets, HUD and effects keep
            // their original order and behavior.
            if (vitaCh2BattleOverlay) continue;
#endif
            VMContext* ctx = runner->vmContext;
            Instance* savedInstance = ctx->currentInstance;
            int32_t savedEventType = ctx->currentEventType;
            int32_t savedEventSubtype = ctx->currentEventSubtype;
            
            ctx->currentInstance = ctx->globalScopeInstance;
            ctx->currentEventType = EVENT_DRAW;
            ctx->currentEventSubtype = DRAW_NORMAL;
            
            if (runtimeLayer->beginScript >= 0)
                VM_callCodeIndex(ctx, runtimeLayer->beginScript, nullptr, 0);
            
            ctx->currentInstance = savedInstance;
            ctx->currentEventType = savedEventType;
            ctx->currentEventSubtype = savedEventSubtype;
            float layerOffsetX = runtimeLayer->xOffset;
            float layerOffsetY = runtimeLayer->yOffset;

            // Handle layer elements
            if (runner->renderer != nullptr) {
                float roomW = (float) runner->currentRoom->width;
                float roomH = (float) runner->currentRoom->height;
                size_t elementCount = arrlenu(runtimeLayer->elements);
                repeat(elementCount, j) {
                    RuntimeLayerElement* layerElement = &runtimeLayer->elements[j];
                    if (layerElement->type == RuntimeLayerElementType_Background && layerElement->backgroundElement != nullptr) {
                        renderBackgroundElement(runner, layerElement->backgroundElement, roomW, roomH, layerOffsetX, layerOffsetY);
                    }
                }
            }

            // Everything after this point is static/parsed layers from the Room itself
            RoomLayer* parsedLayer = Runner_findRoomLayerById(runner->currentRoom, (int32_t) runtimeLayer->id);
            if (parsedLayer == nullptr) continue;
            if (parsedLayer->type == RoomLayerType_Assets) {
                RoomLayerAssetsData* data = parsedLayer->assetsData;
                size_t tileElementCount = arrlenu(runtimeLayer->elements);
                repeat(data->legacyTileCount, j) {
                    if (runner->renderer != nullptr) {
                        RoomTile* tile = &data->legacyTiles[j];
                        // Find the matching RuntimeLayerElement so we can honor per-element visibility
                        RuntimeLayerElement* tileEl = nullptr;
                        repeat(tileElementCount, k) {
                            RuntimeLayerElement* candidate = &runtimeLayer->elements[k];
                            if (candidate->type == RuntimeLayerElementType_Tile && candidate->tileElement == tile) {
                                tileEl = candidate;
                                break;
                            }
                        }
                        if (tileEl != nullptr && !tileEl->visible) continue;
                        // Check if this tile's layer is hidden via tile_layer_hide()
                        ptrdiff_t layerIdx = hmgeti(runner->tileLayerMap, tile->tileDepth);
                        if (layerIdx >= 0 && !runner->tileLayerMap[layerIdx].value.visible) continue;
                        float offsetX = 0.0f, offsetY = 0.0f;
                        if (layerIdx >= 0) {
                            offsetX = runner->tileLayerMap[layerIdx].value.offsetX;
                            offsetY = runner->tileLayerMap[layerIdx].value.offsetY;
                        }

#ifdef ENABLE_VM_TRACING
                        // Trace tile drawing if requested
                        if (shlen(runner->vmContext->tilesToBeTraced) > 0) {
                            DataWin* dataWin = runner->dataWin;
                            const char* bgName = (tile->backgroundDefinition >= 0 && dataWin->bgnd.count > (uint32_t) tile->backgroundDefinition) ? dataWin->bgnd.backgrounds[tile->backgroundDefinition].name : "<none>";
                            const char* roomName = room->name;

                            bool shouldTrace = shgeti(runner->vmContext->tilesToBeTraced, "*") != -1 || shgeti(runner->vmContext->tilesToBeTraced, bgName) != -1 || shgeti(runner->vmContext->tilesToBeTraced, roomName) != -1;

                            if (shouldTrace) {
                                int32_t tpagIndex = Renderer_resolveObjectTPAGIndex(dataWin, tile);
                                if (tpagIndex >= 0) {
                                    TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];
                                    fprintf(stderr, "Runner: [%s] Drawing tile #%d bg=%s(%d) tpag(srcX=%d srcY=%d srcW=%d srcH=%d tgtX=%d tgtY=%d bndW=%d bndH=%d page=%d) tile(srcX=%d srcY=%d w=%u h=%u) at pos=(%d,%d) depth=%d\n", roomName, d->tileIndex, bgName, tile->backgroundDefinition, tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight, tpag->targetX, tpag->targetY, tpag->boundingWidth, tpag->boundingHeight, tpag->texturePageId, tile->sourceX, tile->sourceY, tile->width, tile->height, tile->x, tile->y, tile->tileDepth);

                                    // Warn if tile source rect exceeds TPAG content bounds
                                    if ((uint32_t) (tile->sourceX + tile->width) > (uint32_t) tpag->sourceWidth || (uint32_t) (tile->sourceY + tile->height) > (uint32_t) tpag->sourceHeight) {
                                        fprintf(stderr, "Runner: [%s] WARNING: Tile #%d source rect (%d,%d %ux%u) exceeds TPAG content bounds (%dx%d)\n", roomName, d->tileIndex, tile->sourceX, tile->sourceY, tile->width, tile->height, tpag->sourceWidth, tpag->sourceHeight);
                                    }
                                } else {
                                    fprintf(stderr, "Runner: [%s] Drawing tile #%d bg=%s(%d) tpag=UNRESOLVED tile(srcX=%d srcY=%d w=%u h=%u) at pos=(%d,%d) depth=%d\n", roomName, d->tileIndex, bgName, tile->backgroundDefinition, tile->sourceX, tile->sourceY, tile->width, tile->height, tile->x, tile->y, tile->tileDepth);
                                }
                            }
                        }
#endif

                        RoomTile runtimeTile = *tile;
                        if (tileEl != nullptr) runtimeTile.alpha = tileEl->alpha;
                        Renderer_drawTile(runner->renderer, &runtimeTile, offsetX, offsetY);
                    }
                }

                // Sprite elements are rendered from the runtime element list (not the parsed data) so that layer_sprite_destroy can remove them at runtime.
                size_t elementCount = arrlenu(runtimeLayer->elements);
                repeat(elementCount, j) {
                    if (runner->renderer == nullptr) break;
                    RuntimeLayerElement* el = &runtimeLayer->elements[j];
                    if (el->type != RuntimeLayerElementType_Sprite || el->spriteElement == nullptr) continue;
                    RuntimeSpriteElement* spr = el->spriteElement;
                    if (0 > spr->spriteIndex) continue;
                    Renderer_drawSpriteExt(
                        runner->renderer, spr->spriteIndex, (int32_t) spr->frameIndex,
                        (float) spr->x + layerOffsetX, (float) spr->y + layerOffsetY, spr->scaleX,
                        spr->scaleY, spr->rotation, el->blend,
                        el->alpha);
                }
            } else if (parsedLayer->type == RoomLayerType_Tiles) {
                if (runner->renderer == nullptr) continue;
                for (size_t j = 0; j < arrlenu(runtimeLayer->elements); ++j) {
                    RuntimeLayerElement* el = &runtimeLayer->elements[j];
                    if (el->type == RuntimeLayerElementType_Tilemap && el->visible)
                        Runner_drawTileLayer(runner, el->tilemapData, layerOffsetX, layerOffsetY, el->tilemapFrame);
                }
            } else if (parsedLayer->type == RoomLayerType_Background) {
                // Nothing to render here: handled above
            } else if (parsedLayer->type == RoomLayerType_Instances) {
                // Nothing to render here: handled above on the DRAWABLE_INSTANCE path
            } else if (parsedLayer->type == RoomLayerType_Path || parsedLayer->type == RoomLayerType_Path2) {
                // Nothing to render: not used for rendering purposes
            } else if (parsedLayer->type == RoomLayerType_Effect) {
                // TODO: Implement post-processing effect layers!
            }
            ctx->currentInstance = ctx->globalScopeInstance;
            ctx->currentEventType = EVENT_DRAW;
            ctx->currentEventSubtype = DRAW_NORMAL;
            
            if (runtimeLayer->endScript >= 0)
                VM_callCodeIndex(ctx, runtimeLayer->endScript, nullptr, 0);
            
            ctx->currentInstance = savedInstance;
            ctx->currentEventType = savedEventType;
            ctx->currentEventSubtype = savedEventSubtype;
        }
    }

    fireDrawSubtype(runner, runner->cachedDrawables, drawableCount, DRAW_END);

    // Draw foreground backgrounds (in front of instances, behind GUI)
    drawGMS1Backgrounds(runner, true);
}

// Open a GUI-space draw pass and remember its parameters so surface_reset_target can restore the GUI target + projection when the surface stack pops back to empty mid-pass.
static void beginGuiPass(Runner* runner, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, int32_t targetSurfaceId) {
    runner->inGuiPass = true;
    runner->guiPassW = guiW;
    runner->guiPassH = guiH;
    runner->guiPassPortW = portW;
    runner->guiPassPortH = portH;
    runner->guiPassTarget = targetSurfaceId;
    runner->renderer->vtable->beginGUI(runner->renderer, guiW, guiH, 0, 0, portW, portH, targetSurfaceId);
}

static void endGuiPass(Runner* runner) {
    runner->renderer->vtable->endGUI(runner->renderer);
    runner->inGuiPass = false;
}

void Runner_drawGUI(Runner* runner, int32_t windowW, int32_t windowH, int32_t targetW, int32_t targetH) {
    rebuildDrawableCacheIfDirty(runner);
    Drawable* drawables = runner->cachedDrawables;
    int32_t drawableCount = (int32_t) arrlen(drawables);

    int32_t guiW = runner->guiWidth > 0 ? runner->guiWidth : targetW;
    int32_t guiH = runner->guiHeight > 0 ? runner->guiHeight : targetH;
    beginGuiPass(runner, guiW, guiH, windowW, windowH, RENDER_TARGET_HOST_FRAMEBUFFER);
    fireDrawSubtype(runner, drawables, drawableCount, DRAW_GUI_BEGIN);
    fireDrawSubtype(runner, drawables, drawableCount, DRAW_GUI);
    fireDrawSubtype(runner, drawables, drawableCount, DRAW_GUI_END);
    endGuiPass(runner);
}

void Runner_drawPre(Runner* runner, int32_t windowW, int32_t windowH) {
    runner->profDrawMaxUs = 0;
    runner->profDrawMaxObject = -1;
    rebuildDrawableCacheIfDirty(runner);
    Drawable* drawables = runner->cachedDrawables;
    int32_t drawableCount = (int32_t) arrlen(drawables);

    // Pre Draw runs before the app-surface blit, so it targets the application surface.
    beginGuiPass(runner, windowW, windowH, windowW, windowH, runner->applicationSurfaceId);
    fireDrawSubtype(runner, drawables, drawableCount, DRAW_PRE);
    endGuiPass(runner);
}

void Runner_drawPost(Runner* runner, int32_t windowW, int32_t windowH) {
    rebuildDrawableCacheIfDirty(runner);
    Drawable* drawables = runner->cachedDrawables;
    int32_t drawableCount = (int32_t) arrlen(drawables);

    beginGuiPass(runner, windowW, windowH, windowW, windowH, RENDER_TARGET_HOST_FRAMEBUFFER);
    fireDrawSubtype(runner, drawables, drawableCount, DRAW_POST);
    endGuiPass(runner);
}

void Runner_computeViewDisplayScale(Runner* runner, int32_t gameW, int32_t gameH, float* outScaleX, float* outScaleY) {
    *outScaleX = 1.0f;
    *outScaleY = 1.0f;

    bool viewsEnabled = runner->viewsEnabled;
    if (viewsEnabled) {
        int32_t minLeft = INT32_MAX, minTop = INT32_MAX;
        int32_t maxRight = INT32_MIN, maxBottom = INT32_MIN;
        repeat(MAX_VIEWS, vi) {
            RuntimeView* view = &runner->views[vi];
            if (!view->enabled) continue;
            // Surface-backed views are off-screen cameras. Their small ports
            // must not change the scale or position of the main viewport.
            if (view->surfaceId != -1) continue;
            if (minLeft > view->portX) minLeft = view->portX;
            if (minTop > view->portY) minTop = view->portY;
            int32_t right = view->portX + view->portWidth;
            int32_t bottom = view->portY + view->portHeight;
            if (right > maxRight) maxRight = right;
            if (bottom > maxBottom) maxBottom = bottom;
        }
        if (maxRight > minLeft && maxBottom > minTop) {
            *outScaleX = (float) gameW / (float) (maxRight - minLeft);
            *outScaleY = (float) gameH / (float) (maxBottom - minTop);
        }
    }
}

// Widescreen hack: widen one view axis from baseSize to surfaceSize while keeping the same pixels-per-world scale, growing the visible area symmetrically about the view center.
static void expandViewAxis(int32_t pos, int32_t size, int32_t surfaceSize, int32_t baseSize, int32_t* outPos, int32_t* outSize) {
    if (surfaceSize <= baseSize || baseSize <= 0) {
        *outPos = pos;
        *outSize = size;
        return;
    }
    int32_t center = pos + size / 2;
    *outSize = (int32_t) ((int64_t) size * surfaceSize / baseSize);
    *outPos = center - *outSize / 2;
}

// Applies the visual-only free camera (pan + zoom) on top of a view rectangle, in place.
static void applyFreeCamera(Runner* runner, int32_t* viewX, int32_t* viewY, int32_t* viewW, int32_t* viewH) {
    float zoom = runner->freeCamZoom;
    if (0.0f >= zoom) zoom = 1.0f;
    if (zoom == 1.0f && runner->freeCamPanX == 0.0f && runner->freeCamPanY == 0.0f) return;

    float baseW = (float) *viewW;
    float baseH = (float) *viewH;
    float zoomedW = baseW / zoom;
    float zoomedH = baseH / zoom;
    float centerX = (float) *viewX + baseW * 0.5f + runner->freeCamPanX * baseW;
    float centerY = (float) *viewY + baseH * 0.5f + runner->freeCamPanY * baseH;

    *viewW = (int32_t) zoomedW;
    *viewH = (int32_t) zoomedH;
    *viewX = (int32_t) (centerX - zoomedW * 0.5f);
    *viewY = (int32_t) (centerY - zoomedH * 0.5f);
}

void Runner_drawViews(Runner* runner, int32_t gameW, int32_t gameH, bool debugShowCollisionMasks) {
    Renderer* renderer = runner->renderer;
    renderer->vtable->clearScreen(renderer, runner->drawBackgroundColor ? runner->backgroundColor : 0, 1.0f);

    bool anyViewRendered = false;

    bool viewsEnabled = runner->viewsEnabled;

    int32_t widescreenBaseW = gameW - runner->widescreenExtraWidth;
    int32_t widescreenBaseH = gameH - runner->widescreenExtraHeight;

    if (viewsEnabled) {
        repeat(MAX_VIEWS, vi) {
            RuntimeView* view = &runner->views[vi];
            if (!view->enabled) continue;
            // Geometry comes from the assigned camera (source of truth); the viewport (port) stays on the view.
            GMLCamera* camera = Runner_getCameraForView(runner, (int32_t) vi);
            if (camera == nullptr) continue;

            bool toSurface = view->surfaceId != -1;

            if (toSurface) {
                // The surface is GONE, skip it!
                if (!renderer->vtable->surfaceExists(renderer, view->surfaceId))
                    continue;

#ifdef PLATFORM_VITA
                // The Garden sakura transition renders a complete second
                // 640x480 world view into surf_view1. On Vita this roughly
                // doubles room cost (16-20 render-target switches/frame).
                // Reuse the previous surface every other frame: the primary
                // view, battle, input and GUI remain full-rate, while this
                // decorative transition background updates at 15 Hz.
                extern int g_vitaActiveChapter;
                if (g_vitaActiveChapter == 5 && vi == 1 &&
                    runner->currentRoom != nullptr && runner->currentRoom->name != nullptr &&
                    strcmp(runner->currentRoom->name, "room_dw_garden_floradinnencounter") == 0 &&
                    (runner->frameCount & 1) != 0) {
                    anyViewRendered = true;
                    continue;
                }
#endif

                // Target selection uses viewCurrent to choose the surface camera.
                // Select this view before binding, including the first draw/clear.
                runner->viewCurrent = (int32_t) vi;
                Runner_surfaceSetTarget(runner, view->surfaceId);

                if (runner->drawBackgroundColor)
                    renderer->vtable->clearScreen(renderer, runner->currentRoom->backgroundColor, 1.0f);

                runner->viewCurrent = (int32_t) vi;
                runner->renderer->cameraCurrent = runner->views[runner->viewCurrent].cameraId;
                runner->renderer->vtable->applyProjection(runner->renderer, &camera->viewMatrix, &camera->projectionMatrix);

                Runner_draw(runner);

                renderer->vtable->flush(renderer);

                Runner_surfaceResetTarget(runner);
                anyViewRendered = true;
                continue;
            }

            int32_t viewX, viewY, viewW, viewH;
            expandViewAxis(camera->viewX, camera->viewWidth, gameW, widescreenBaseW, &viewX, &viewW);
            expandViewAxis(camera->viewY, camera->viewHeight, gameH, widescreenBaseH, &viewY, &viewH);
            applyFreeCamera(runner, &viewX, &viewY, &viewW, &viewH);
            int32_t portX = (int32_t) ((float) view->portX * runner->displayScaleX + 0.5f);
            int32_t portY = (int32_t) ((float) view->portY * runner->displayScaleY + 0.5f);
            int32_t portW = (int32_t) ((float) view->portWidth * runner->displayScaleX + 0.5f);
            int32_t portH = (int32_t) ((float) view->portHeight * runner->displayScaleY + 0.5f);
            float viewAngle = camera->viewAngle;

            runner->viewCurrent = (int32_t) vi;
            runner->renderer->cameraCurrent = runner->views[runner->viewCurrent].cameraId;
            renderer->vtable->beginView(renderer, viewX, viewY, viewW, viewH, portX, portY, portW, portH, viewAngle);

            Runner_draw(runner);

            if (debugShowCollisionMasks) DebugOverlay_drawCollisionMasks(runner);

            renderer->vtable->endView(renderer);

            anyViewRendered = true;
        }
    }

    if (!anyViewRendered) {
        runner->viewCurrent = 0;
        GMLCamera* camera = Runner_getCameraForView(runner, (int32_t) runner->viewCurrent);
        runner->renderer->cameraCurrent = runner->views[runner->viewCurrent].cameraId;
        // See GameMaker-HTML5's "DrawViews", in specific the !m_enableviews path
        // When views aren't used, the room width/height is used
        int32_t viewX, viewY, viewW, viewH;
        expandViewAxis(0, (int32_t) runner->currentRoom->width, gameW, widescreenBaseW, &viewX, &viewW);
        expandViewAxis(0, (int32_t) runner->currentRoom->height, gameH, widescreenBaseH, &viewY, &viewH);
        applyFreeCamera(runner, &viewX, &viewY, &viewW, &viewH);
        //make default projection
        if (camera != nullptr) {
            camera->viewX = 0;
            camera->viewY = 0;
            camera->viewWidth = runner->currentRoom->width;
            camera->viewHeight = runner->currentRoom->height;
            camera->viewAngle = 0.0;
            Runner_updateCameraViewSimple(camera);
        }

        renderer->vtable->beginView(renderer, viewX, viewY, viewW, viewH, 0, 0, gameW, gameH, 0);

        Runner_draw(runner);

        if (debugShowCollisionMasks) DebugOverlay_drawCollisionMasks(runner);

        renderer->vtable->endView(renderer);

    }

    // Reset view_current to 0 so non-Draw events (Step, Alarm, Create) see view_current = 0
    runner->viewCurrent = 0;
    runner->renderer->cameraCurrent = runner->views[runner->viewCurrent].cameraId;
}

// ===[ Instance Creation Helper ]===

static bool isObjectDisabled(Runner* runner, int32_t objectIndex) {
    if (runner->disabledObjects == nullptr) return false;
    const char* name = runner->dataWin->objt.objects[objectIndex].name;
    return shgeti(runner->disabledObjects, name) != -1;
}

static Instance* createAndInitInstance(Runner* runner, int32_t instanceId, int32_t objectIndex, GMLReal x, GMLReal y) {
    DataWin* dataWin = runner->dataWin;
    require(objectIndex >= 0 && dataWin->objt.count > (uint32_t) objectIndex);

    GameObject* objDef = &dataWin->objt.objects[objectIndex];

    Instance* inst = Instance_create(instanceId, objectIndex, x, y);

    // Copy properties from object definition
    inst->spriteIndex = objDef->spriteId;
    inst->visible = objDef->visible;
    inst->solid = objDef->solid;
    inst->persistent = objDef->persistent;
    inst->depth = objDef->depth;
    inst->maskIndex = objDef->textureMaskId;

    hmput(runner->instancesById, instanceId, inst);
    arrput(runner->instances, inst);
    Runner_addInstanceToObjectLists(runner, inst);
    runner->drawableListStructureDirty = true;

#ifdef ENABLE_VM_TRACING
    if (shgeti(runner->vmContext->instanceLifecyclesToBeTraced, "*") != -1 || shgeti(runner->vmContext->instanceLifecyclesToBeTraced, objDef->name) != -1) {
        fprintf(stderr, "VM: Instance %s (instanceId=%d,objectIndex=%d) created at (%f, %f)\n", objDef->name, instanceId, inst->objectIndex, x, y);
    }
#endif

    return inst;
}

// ===[ Room Management ]===

// Collect persistent instances from the previous room (they travel with the player), and free the rest.
// You should re-append them at the tail AFTER creating the new room's own instances, so the iteration order matches the native runner: room-local instances first, persistent arrivals last.
static Instance** takePersistentInstances(Runner* runner) {
    Instance** carriedPersistent = nullptr;
    int32_t oldCount = (int32_t) arrlen(runner->instances);
    repeat(oldCount, i) {
        Instance* inst = runner->instances[i];
        if (inst->persistent) {
#ifdef ENABLE_VM_TRACING
            GameObject* gameObject = &runner->dataWin->objt.objects[inst->objectIndex];
            if (shgeti(runner->vmContext->instanceLifecyclesToBeTraced, "*") != -1 || shgeti(runner->vmContext->instanceLifecyclesToBeTraced, gameObject->name) != -1) {
                fprintf(stderr, "VM: Instance %s (instanceId=%d,objectIndex=%d) has been persisted at (%f, %f) due to room change\n", gameObject->name, inst->instanceId, inst->objectIndex, inst->x, inst->y);
            }
#endif

            // The spatial grid is recreated per room, so any cell coordinates the instance was tracking belong to the old grid and must not be reused.
            arrsetlen(inst->collisionCells, 0);
            inst->spatialGridDirty = false;

            arrput(carriedPersistent, inst);
        } else {
#ifdef ENABLE_VM_TRACING
            GameObject* gameObject = &runner->dataWin->objt.objects[inst->objectIndex];
            if (shgeti(runner->vmContext->instanceLifecyclesToBeTraced, "*") != -1 || shgeti(runner->vmContext->instanceLifecyclesToBeTraced, gameObject->name) != -1) {
                fprintf(stderr, "VM: Instance %s (instanceId=%d,objectIndex=%d) destroyed at (%f, %f) due to room change\n", gameObject->name, inst->instanceId, inst->objectIndex, inst->x, inst->y);
            }
#endif

            hmdel(runner->instancesById, inst->instanceId);
            Runner_executeEvent(runner, inst, EVENT_CLEANUP, 0);
            Runner_removeInstanceFromObjectLists(runner, inst);
            Instance_free(inst);
        }
    }

    arrfree(runner->instances);
    runner->instances = nullptr;

    // The per-object lists referenced both the freed non-persistents and the carried persistents; clear them entirely.
    // Persistents are re-added when they return via returnPersistentInstances, and room-local instances are added as they get created.
    Runner_clearAllObjectLists(runner);

    return carriedPersistent;
}

// Append the carried-over persistent instances at the tail of runner->instances and free the temporary array. Pairs with takePersistentInstances.
static void returnPersistentInstances(Runner* runner, Instance** carriedPersistent) {
    repeat(arrlen(carriedPersistent), i) {
        arrput(runner->instances, carriedPersistent[i]);
        Runner_addInstanceToObjectLists(runner, carriedPersistent[i]);
    }
    arrfree(carriedPersistent);
}

GMLCamera* Runner_getCameraById(Runner* runner, int32_t id) {
    GMLCamera* camera;
    if (0 > id) return nullptr;
    else if (MAX_DEFAULT_ROOM_CAMERAS > id) camera = &runner->defaultCameras[id];
    else if (MAX_CAMERAS > id) camera = &runner->userCameras[id - MAX_DEFAULT_ROOM_CAMERAS];
    else if (id == SURFACE_CAMERA) camera = &runner->surfaceCamera;
    else if (id == GUI_CAMERA) camera = &runner->guiCamera;
    else return nullptr;
    if (!camera->allocated) return nullptr;
    return camera;
}

GMLCamera* Runner_getCameraForView(Runner* runner, int32_t viewIndex) {
    if (0 > viewIndex || viewIndex >= MAX_VIEWS) return nullptr;
    return Runner_getCameraById(runner, runner->views[viewIndex].cameraId);
}

// Populates a default camera (slot == view index) from parsed room view data.
static void initDefaultCameraFromRoomView(GMLCamera* camera, RoomView* roomView) {
    camera->allocated = true;
    camera->viewX = roomView->viewX;
    camera->viewY = roomView->viewY;
    camera->viewWidth = roomView->viewWidth;
    camera->viewHeight = roomView->viewHeight;
    camera->borderX = roomView->borderX;
    camera->borderY = roomView->borderY;
    camera->speedX = roomView->speedX;
    camera->speedY = roomView->speedY;
    camera->objectId = roomView->objectId;
    camera->viewAngle = 0;
    Runner_updateCameraViewSimple(camera);

}

// Copies the viewport (port) properties and enabled flag from parsed room data.
// Geometry goes to the camera (see initDefaultCameraFromRoomView); cameraId is assigned by the caller, which knows the view index.
static void copyRoomViewToRuntimeView(RoomView* roomView, RuntimeView* runtimeView) {
    runtimeView->enabled = roomView->enabled;
    runtimeView->portX = roomView->portX;
    runtimeView->portY = roomView->portY;
    runtimeView->portWidth = roomView->portWidth;
    runtimeView->portHeight = roomView->portHeight;
}

static void initRoom(Runner* runner, int32_t roomIndex) {
    DataWin* dataWin = runner->dataWin;
    require(roomIndex >= 0 && dataWin->room.count > (uint32_t) roomIndex);

    Room* room = &dataWin->room.rooms[roomIndex];

    // Lazy-room load: if the payload wasn't loaded, read it from the data.win file now before anything touches the room's game objects/tiles/layers.
    if (!room->payloadLoaded) {
        DataWin_loadRoomPayload(dataWin, roomIndex);
    }

    SavedRoomState* savedState = &runner->savedRoomStates[roomIndex];

    // Kept so carried persistent instances can be re-homed onto the new room's layer with the same name.
    Room* previousRoom = runner->currentRoom;

    runner->currentRoom = room;
    runner->currentRoomIndex = roomIndex;
    runner->viewsEnabled = (room->flags & 1) != 0;
    // Tile set, runtime layers, and instance list all change when entering a room.
    runner->drawableListStructureDirty = true;
    // It could be the first time we are initializing the grid
    if (runner->spatialGrid != nullptr)
        SpatialGrid_free(runner->spatialGrid);
    runner->spatialGrid = SpatialGrid_create(room->width, room->height);

    // Find position in room order
    runner->currentRoomOrderPosition = -1;
    repeat(dataWin->gen8.roomOrderCount, i) {
        if (dataWin->gen8.roomOrder[i] == roomIndex) {
            runner->currentRoomOrderPosition = (int32_t) i;
            break;
        }
    }

    // If this is a persistent room that was previously visited, restore saved state
    if (room->persistent && savedState->initialized) {
        memcpy(runner->views, savedState->views, sizeof(runner->views));
        runner->viewsEnabled = savedState->viewsEnabled;
        // Restore the room-scoped default cameras (whole array); user cameras are global and left untouched.
        memcpy(runner->defaultCameras, savedState->defaultCameras, sizeof(runner->defaultCameras));

        // Restore backgrounds from saved state
        memcpy(runner->backgrounds, savedState->backgrounds, sizeof(runner->backgrounds));
        runner->backgroundColor = savedState->backgroundColor;
        runner->drawBackgroundColor = savedState->drawBackgroundColor;

        // Restore tile layer map
        hmfree(runner->tileLayerMap);
        runner->tileLayerMap = savedState->tileLayerMap;
        savedState->tileLayerMap = nullptr;

        // Restore runtime layers
        freeRuntimeLayersArray(&runner->runtimeLayers);
        runner->runtimeLayers = savedState->runtimeLayers;
        savedState->runtimeLayers = nullptr;

        Instance** carriedPersistent = takePersistentInstances(runner);

        // The native runner restores the room's own linked list first, then appends persistent arrivals at the tail.
        // Event iteration is forward (oldest first), so a persistent instance runs after the room's own instances.
        int32_t savedCount = (int32_t) arrlen(savedState->instances);
        repeat(savedCount, i) {
            arrput(runner->instances, savedState->instances[i]);
            Runner_addInstanceToObjectLists(runner, savedState->instances[i]);
        }
        arrfree(savedState->instances);
        savedState->instances = nullptr;

        returnPersistentInstances(runner, carriedPersistent);

        // No Create events, no preCreateCode, no creationCode, no room creation code
        fprintf(stderr, "Runner: Room restored (persistent): %s (room %d) with %d instances\n", room->name, roomIndex, (int) arrlen(runner->instances));
        return;
    }

    // === Normal room initialization (first visit, or non-persistent room) ===

    // Initialize the views and their default cameras from scratch.
    repeat(MAX_VIEWS, vi) {
        RoomView* roomView = &room->views[vi];
        copyRoomViewToRuntimeView(roomView, &runner->views[vi]);
        initDefaultCameraFromRoomView(&runner->defaultCameras[vi], roomView);
        runner->views[vi].cameraId = (int32_t) vi;
        runner->views[vi].surfaceId = -1;
    }

    // Reset tile layer state for the new room
    hmfree(runner->tileLayerMap);
    runner->tileLayerMap = nullptr;

    // Populate runtime layers from parsed room layers (GMS2+ only; empty for GMS1.x).
    // Dynamic layers created via layer_create are appended to this array later.
    freeRuntimeLayersArray(&runner->runtimeLayers);
    uint32_t maxLayerId = 0;
    repeat(room->layerCount, i) {
        RoomLayer* layerSource = &room->layers[i];
        RuntimeLayer runtimeLayer = {0};
        runtimeLayer.id = layerSource->id;
        runtimeLayer.depth = layerSource->depth;
        runtimeLayer.visible = layerSource->visible;
        runtimeLayer.xOffset = layerSource->xOffset;
        runtimeLayer.yOffset = layerSource->yOffset;
        runtimeLayer.hSpeed = layerSource->hSpeed;
        runtimeLayer.vSpeed = layerSource->vSpeed;
        runtimeLayer.dynamic = false;
        runtimeLayer.beginScript = -1;
        runtimeLayer.endScript = -1;
        arrput(runner->runtimeLayers, runtimeLayer);
        if (layerSource->id > maxLayerId) maxLayerId = layerSource->id;
    }
    // Watermark: ensure runtime-allocated IDs (layers + elements) stay above parsed IDs.
    if (maxLayerId >= runner->nextLayerId) runner->nextLayerId = maxLayerId + 1;

    // Convert room layers into runtime elements
    repeat(room->layerCount, i) {
        RoomLayer* layerSource = &room->layers[i];
        if (layerSource->type == RoomLayerType_Background && layerSource->backgroundData != nullptr) {
            RoomLayerBackgroundData* src = layerSource->backgroundData;
            RuntimeBackgroundElement* bg = (RuntimeBackgroundElement *)safeMalloc(sizeof(RuntimeBackgroundElement));
            bg->spriteIndex = src->spriteIndex;
            bg->visible = src->visible;
            bg->hTiled = src->hTiled;
            bg->vTiled = src->vTiled;
            bg->stretch = src->stretch;
            bg->xScale = 1.0f;
            bg->yScale = 1.0f;
            bg->blend = src->color & 0xFFFFFFu;
            bg->alpha = (float) BGR_A(src->color) / 255.0f;
            bg->xOffset = 0.0f;
            bg->yOffset = 0.0f;
            bg->imageIndex = src->imageIndex;
            RuntimeLayerElement el = {0};
            el.id = Runner_getNextLayerId(runner);
            el.type = RuntimeLayerElementType_Background;
            el.visible = true;
            el.alpha = bg->alpha;
            el.blend = bg->blend;
            el.backgroundElement = bg;
            arrput(runner->runtimeLayers[i].elements, el);
            continue;
        }
        if (layerSource->type == RoomLayerType_Tiles && layerSource->tilesData != nullptr) {
            RuntimeLayerElement el = {0};
            el.id = Runner_getNextLayerId(runner);
            el.type = RuntimeLayerElementType_Tilemap;
            el.visible = true;
            el.alpha = 1.0f;
            el.blend = 0xFFFFFFu;
            el.tilemapData = layerSource->tilesData;
            arrput(runner->runtimeLayers[i].elements, el);
            continue;
        }
        if (layerSource->type != RoomLayerType_Assets || layerSource->assetsData == nullptr) continue;
        RoomLayerAssetsData* assets = layerSource->assetsData;
        RuntimeLayer* runtimeLayer = &runner->runtimeLayers[i];
        repeat(assets->spriteCount, j) {
            SpriteInstance* src = &assets->sprites[j];
            RuntimeSpriteElement* spriteElement = (RuntimeSpriteElement *)safeMalloc(sizeof(RuntimeSpriteElement));
            spriteElement->name = src->name;
            spriteElement->spriteIndex = src->spriteIndex;
            spriteElement->x = src->x;
            spriteElement->y = src->y;
            spriteElement->scaleX = src->scaleX;
            spriteElement->scaleY = src->scaleY;
            spriteElement->color = src->color;
            spriteElement->animationSpeed = src->animationSpeed;
            spriteElement->animationSpeedType = src->animationSpeedType;
            spriteElement->frameIndex = src->frameIndex;
            spriteElement->rotation = src->rotation;
            RuntimeLayerElement el = {0};
            el.id = Runner_getNextLayerId(runner);
            el.type = RuntimeLayerElementType_Sprite;
            el.visible = true;
            el.alpha = (float) BGR_A(spriteElement->color) / 255.0f;
            el.blend = spriteElement->color & 0xFFFFFFu;
            el.spriteElement = spriteElement;
            arrput(runtimeLayer->elements, el);
        }
        // Expose legacy tiles as RuntimeLayerElements so GML scripts can find them via layer_get_all_elements and toggle them via layer_tile_visible
        repeat(assets->legacyTileCount, j) {
            RoomTile* tile = &assets->legacyTiles[j];
            RuntimeLayerElement el = {0};
            el.id = Runner_getNextLayerId(runner);
            el.type = RuntimeLayerElementType_Tile;
            el.visible = true;
            el.alpha = tile->alpha;
            el.blend = tile->color & 0xFFFFFFu;
            el.backgroundElement = nullptr;
            el.spriteElement = nullptr;
            el.tileElement = tile;
            arrput(runtimeLayer->elements, el);
        }
    }

    // Copy room background definitions into mutable runtime state
    runner->backgroundColor = room->backgroundColor;
    runner->drawBackgroundColor = room->drawBackgroundColor;
    repeat(8, i) {
        RoomBackground* src = &room->backgrounds[i];
        RuntimeBackground* dst = &runner->backgrounds[i];
        dst->visible = src->enabled;
        dst->foreground = src->foreground;
        dst->backgroundIndex = src->backgroundDefinition;
        dst->x = (float) src->x;
        dst->y = (float) src->y;
        dst->tileX = (bool) src->tileX;
        dst->tileY = (bool) src->tileY;
        dst->speedX = (float) src->speedX;
        dst->speedY = (float) src->speedY;
        dst->xScale = 1.0f;
        dst->yScale = 1.0f;
        dst->stretch = src->stretch;
        dst->alpha = 1.0f;
    }

    Instance** carriedPersistent = takePersistentInstances(runner);

    // Re-home carried persistent instances onto the new room's layer with the same name as their old layer (native runner behavior).
    // Layer IDs are unique per room, so the old ID never matches a new-room layer directly.
    repeat(arrlen(carriedPersistent), ci) {
        Instance* inst = carriedPersistent[ci];
        if (0 > inst->layer || previousRoom == nullptr) continue;
        RoomLayer* oldLayer = Runner_findRoomLayerById(previousRoom, inst->layer);
        int32_t newLayerId = -1;
        int32_t newLayerDepth = inst->depth;
        if (oldLayer != nullptr) {
            const char* oldLayerName = oldLayer->name;
            // Search both the new room's parsed layers and any dynamic layers already created.
            size_t runtimeCount = arrlenu(runner->runtimeLayers);
            repeat(runtimeCount, li) {
                RuntimeLayer* runtimeLayer = &runner->runtimeLayers[li];
                const char* runtimeLayerName = runtimeLayer->dynamicName;

                // Get the ORIGINAL name of the layer if available, not the one that may have been changed during runtime
                RoomLayer* roomLayer = Runner_findRoomLayerById(room, (int32_t) runtimeLayer->id);
                if (roomLayer != nullptr)
                    runtimeLayerName = roomLayer->name;

                if (strcmp(runtimeLayerName, oldLayerName) == 0) {
                    newLayerId = (int32_t) runtimeLayer->id;
                    newLayerDepth = runtimeLayer->depth;
                    break;
                }
            }

            if (0 > newLayerId) {
                // No layer with that name in the new room: create one at the instance's depth.
                RuntimeLayer runtimeLayer = {0};
                runtimeLayer.id = Runner_getNextLayerId(runner);
                runtimeLayer.depth = inst->depth;
                runtimeLayer.visible = true;
                runtimeLayer.dynamic = true;
                runtimeLayer.dynamicName = safeStrdup(oldLayerName);
                runtimeLayer.beginScript = -1;
                runtimeLayer.endScript = -1;
                arrput(runner->runtimeLayers, runtimeLayer);
                newLayerId = (int32_t) runtimeLayer.id;
                newLayerDepth = runtimeLayer.depth;
            }
        }
        inst->layer = newLayerId;
        if (newLayerId >= 0) {
            inst->depth = newLayerDepth;
            Runner_addInstanceLayerElement(runner, newLayerId, inst->instanceId);
        }
    }

    // Two-pass instance creation (matches HTML5 runner behavior):
    // Pass 1: Create all instance objects so they exist for cross-references
    // Pass 2: Fire preCreateCode, CREATE events, and creationCode
    // This ensures that when an instance's Create event reads another instance
    // (e.g. obj_mainchara reading obj_markerA.x), the target already exists.

    // Pass 1: Create all instances without firing events
    repeat(room->gameObjectCount, i) {
        RoomGameObject* roomObj = &room->gameObjects[i];

        if (roomObj->objectDefinition == -1) {
            fprintf(stderr, "Runner: Object %d in room %s does not have a valid object definition reference! Was it deleted in the editor?\n", roomObj->instanceID, room->name);
            continue;
        }

        // Skip if a persistent instance carried over from the previous room already owns this ID (re-entering the persistent instance's home room, don't create a duplicate!).
        if (hmget(runner->instancesById, roomObj->instanceID) != nullptr) continue;
        if (isObjectDisabled(runner, roomObj->objectDefinition)) continue;

        Instance* inst = createAndInitInstance(runner, roomObj->instanceID, roomObj->objectDefinition, (GMLReal) roomObj->x, (GMLReal) roomObj->y);
        inst->imageXscale = (float) roomObj->scaleX;
        inst->imageYscale = (float) roomObj->scaleY;
        inst->imageAngle = (float) roomObj->rotation;
        inst->imageSpeed = roomObj->imageSpeed;
        inst->imageIndex = (float) roomObj->imageIndex;
        // Room editor stores per-instance color as ABGR (0xAABBGGRR): low 24 bits feed image_blend, top 8 bits feed image_alpha.
        inst->imageBlend = roomObj->color & 0x00FFFFFF;
        inst->imageAlpha = (float) ((roomObj->color >> 24) & 0xFF) / 255.0f;
    }

    // In GMS2, instances get their depth from their room layer, not the object definition.
    // This must happen before firing Create events so scripts like scr_depth() read the layer depth.
    if (DataWin_isVersionAtLeast(runner->dataWin, 2, 0, 0, 0)) {
        repeat(room->layerCount, li) {
            RoomLayer* layer = &room->layers[li];
            if (layer->type != RoomLayerType_Instances || layer->instancesData == nullptr) continue;
            RoomLayerInstancesData* layerData = layer->instancesData;
            repeat(layerData->instanceCount, ii) {
                Instance* inst = hmget(runner->instancesById, layerData->instanceIds[ii]);
                if (inst != nullptr) {
                    inst->depth = layer->depth;
                    inst->layer = (int32_t) layer->id;
                    Runner_addInstanceLayerElement(runner, (int32_t) layer->id, inst->instanceId);
                }
            }
        }
    }

    // Append persistent instances carried over from the previous room at the tail, so forward event iteration processes the new room's own instances first and the travelers last.
    // We NEED to do this here BEFORE firing the room object's events, to avoid code that relies on persistent instances failing (example: if a object uses instance_number to get the number of instances in the room).
    returnPersistentInstances(runner, carriedPersistent);

    // Pass 2: Fire events for newly created instances (in room definition order)
    repeat(room->gameObjectCount, i) {
        RoomGameObject* roomObj = &room->gameObjects[i];

        Instance* inst = hmget(runner->instancesById, roomObj->instanceID);
        if (inst == nullptr) continue;

        // Skip instances that already had their Create event fired (persistent carry-overs
        // that hmget also matches, since instancesById still holds them).
        if (inst->createEventFired) continue;
        inst->createEventFired = true;

        // An earlier instance's Create event may have destroyed this one, skip it!
        if (inst->destroyed) continue;

        Runner_executeEvent(runner, inst, EVENT_PRECREATE, 0);
        executeCode(runner, inst, roomObj->preCreateCode);
        if (inst->destroyed) continue;
        Runner_executeEvent(runner, inst, EVENT_CREATE, 0);
        if (inst->destroyed) continue;
        executeCode(runner, inst, roomObj->creationCode);
    }

    // Run room creation code
    if (room->creationCodeId >= 0 && dataWin->code.count > (uint32_t) room->creationCodeId) {
        // Room creation code runs in global context, the native runner creates a fake/dummy instance for the "self"
        Instance* dummy = Instance_create(0, STRUCT_OBJECT_INDEX, 0, 0);
        runner->vmContext->currentInstance = dummy;
        RValue result = VM_executeCode(runner->vmContext, room->creationCodeId);
        RValue_free(&result);
        runner->vmContext->currentInstance = nullptr;
        Instance_free(dummy);
    }

    // Mark this room as initialized for persistent room support
    savedState->initialized = true;

    fprintf(stderr, "Runner: Room loaded: %s (room %d) with %d instances\n", room->name, roomIndex, (int) arrlen(runner->instances));
}

// Cleans up the runner state, used when freeing the Runner or when restarting the Runner
static void cleanupState(Runner* runner) {
    // Drop VM-side RValue holders (globals, stack, call frames) BEFORE freeing any Instance memory. This way any RVALUE_STRUCT refs decrement against still-live struct memory; otherwise we'd free a struct here and then have VM_free's later VM_reset try to decRef a dangling pointer.
    if (runner->vmContext != nullptr) {
        VM_reset(runner->vmContext);
    }

    // Free all instances
    repeat(arrlen(runner->instances), i) {
        hmdel(runner->instancesById, runner->instances[i]->instanceId);
        Instance_free(runner->instances[i]);
    }
    arrfree(runner->instances);
    runner->instances = nullptr;

    // Empty the per-object lists. We keep the outer instancesByObject array allocated so Runner_reset can be reused; Runner_free releases it.
    Runner_clearAllObjectLists(runner);

    // Free saved room states
    if (runner->savedRoomStates != nullptr) {
        repeat(runner->dataWin->room.count, i) {
            SavedRoomState* state = &runner->savedRoomStates[i];
            int32_t savedCount = (int32_t) arrlen(state->instances);
            repeat(savedCount, j) {
                hmdel(runner->instancesById, state->instances[j]->instanceId);
                Instance_free(state->instances[j]);
            }
            arrfree(state->instances);
            hmfree(state->tileLayerMap);
            freeRuntimeLayersArray(&state->runtimeLayers);
        }
        free(runner->savedRoomStates);
    }
    runner->savedRoomStates = nullptr;

    // Drain ds_map/ds_list pools BEFORE bulk-freeing struct instances. Their RValue entries may hold RVALUE_STRUCT refs to structs in runner->structInstances, and RValue_free would deref freed memory if the structs are gone.
    repeat((int32_t) arrlen(runner->dsMapPool), i) {
        DsMapEntry* map = runner->dsMapPool[i];
        if (map != nullptr) {
            repeat(shlen(map), j) {
                free(map[j].key);
                RValue_free(&map[j].value);
            }
            shfree(map);
        }
    }
    arrfree(runner->dsMapPool);
    runner->dsMapPool = nullptr;

    repeat((int32_t) arrlen(runner->dsListPool), i) {
        DsList* list = &runner->dsListPool[i];
        repeat(arrlen(list->items), j) {
            RValue_free(&list->items[j]);
        }
        arrfree(list->items);
    }
    arrfree(runner->dsListPool);
    runner->dsListPool = nullptr;

    repeat((int32_t) arrlen(runner->dsQueuePool), i) {
        DsQueue* q = &runner->dsQueuePool[i];
        repeat(arrlen(q->items), j) {
            RValue_free(&q->items[j]);
        }
        arrfree(q->items);
    }
    arrfree(runner->dsQueuePool);
    runner->dsQueuePool = nullptr;

    repeat((int32_t) arrlen(runner->dsPriorityPool), i) {
        DsPriority* p = &runner->dsPriorityPool[i];
        repeat(arrlen(p->items), j) {
            RValue_free(&p->items[j].item);
        }
        arrfree(p->items);
    }
    arrfree(runner->dsPriorityPool);
    runner->dsPriorityPool = nullptr;

    repeat((int32_t) arrlen(runner->dsStackPool), i) {
        DsStack* s = &runner->dsStackPool[i];
        repeat(arrlen(s->items), j) {
            RValue_free(&s->items[j]);
        }
        arrfree(s->items);
    }
    arrfree(runner->dsStackPool);
    runner->dsStackPool = nullptr;

    repeat((int32_t) arrlen(runner->dsGridPool), i) {
        DsGrid* grid = &runner->dsGridPool[i];
        size_t count = (size_t) grid->width * (size_t) grid->height;
        repeat(count, j) {
            RValue_free(&grid->items[j]);
        }
        free(grid->items);
    }
    arrfree(runner->dsGridPool);
    runner->dsGridPool = nullptr;

    // Free struct instances.
    // Anything still here at shutdown is leaked refs or a reference cycle - bulk free regardless of refCount.
    // Because structs can reference each other, we need to free every struct's contents FIRST, then we can free the Instance structs themselves.
    repeat(arrlen(runner->structInstances), i) {
        Instance* s = runner->structInstances[i];
        hmdel(runner->instancesById, s->instanceId);
        s->structRegistryIndex = -1;
        Instance_freeContents(s);
    }
    repeat(arrlen(runner->structInstances), i) {
        free(runner->structInstances[i]);
    }
    arrfree(runner->structInstances);
    runner->structInstances = nullptr;

    hmfree(runner->instancesById);
    runner->instancesById = nullptr;
    hmfree(runner->tileLayerMap);
    runner->tileLayerMap = nullptr;
    freeRuntimeLayersArray(&runner->runtimeLayers);
    shfree(runner->disabledObjects);
    runner->disabledObjects = nullptr;

    // Free mp_grid pool
    repeat((int32_t) arrlen(runner->mpGridPool), i) {
        free(runner->mpGridPool[i].cells);
    }
    arrfree(runner->mpGridPool);
    runner->mpGridPool = nullptr;

    // Free pending async buffer save/load state
    repeat((int32_t) arrlen(runner->asyncBufferGroupOps), i) {
        free(runner->asyncBufferGroupOps[i].filename);
    }
    arrfree(runner->asyncBufferGroupOps);
    runner->asyncBufferGroupOps = nullptr;
    arrfree(runner->asyncSaveLoadQueue);
    runner->asyncSaveLoadQueue = nullptr;
    free(runner->asyncBufferGroupName);
    runner->asyncBufferGroupName = nullptr;
    runner->asyncBufferGroupActive = false;

    // Free INI state
    if (runner->currentIni != nullptr) {
        Ini_free(runner->currentIni);
        runner->currentIni = nullptr;
    }
    free(runner->currentIniPath);
    runner->currentIniPath = nullptr;
    if (runner->cachedIni != nullptr) {
        Ini_free(runner->cachedIni);
        runner->cachedIni = nullptr;
    }
    free(runner->cachedIniPath);
    runner->cachedIniPath = nullptr;

    // Free open text files
    repeat(MAX_OPEN_TEXT_FILES, i) {
        OpenTextFile* file = &runner->openTextFiles[i];
        if (file->isOpen) {
            free(file->content);
            free(file->writeBuffer);
            free(file->filePath);
            *file = (OpenTextFile) {0};
        }
    }

    // Close any binary file handles still held by the game (close flushes write modes
    // through the FileSystem vtable, so an orderly shutdown still persists pending data)
    repeat(MAX_OPEN_BINARY_FILES, i) {
        OpenBinaryFile* file = &runner->openBinaryFiles[i];
        if (file->isOpen) {
            runner->fileSystem->vtable->binaryClose(runner->fileSystem, file->handle);
            *file = (OpenBinaryFile) {0};
        }
    }

    // Free any active file_find_* enumeration session
    repeat(arrlen(runner->fileFindResults), i) {
        free(runner->fileFindResults[i]);
    }
    arrfree(runner->fileFindResults);
    runner->fileFindResults = nullptr;
    runner->fileFindPosition = 0;

    if (runner->spatialGrid != nullptr) {
        SpatialGrid_free(runner->spatialGrid);
        runner->spatialGrid = nullptr;
    }
}

// ===[ Public API ]===

void Runner_reset(Runner* runner) {
    // This actually sets the default runner values, used for initialization and restarting
    cleanupState(runner);

    // Reset VM state
    VM_reset(runner->vmContext);

    runner->pendingRoom = -1;
    runner->asyncLoadMapId = -1;
    runner->asyncBufferNextRequestId = 1;
    runner->xboxAccountPickerPendingId = -1;
    runner->xboxAccountPickerPadIndex = 0;
    runner->xboxAsyncIdCounter = 1;
    runner->score = 0.0;
    runner->lives = -1.0;
    runner->health = 0.0;
    runner->gameStartFired = false;
    runner->currentRoomIndex = -1;
    runner->currentRoomOrderPosition = -1;
    runner->nextInstanceId = runner->dataWin->gen8.lastObj + 1;
    runner->savedRoomStates = (SavedRoomState *)safeCalloc(runner->dataWin->room.count, sizeof(SavedRoomState));
    runner->nextLayerId = 1;
    runner->audioSystem->vtable->stopAll(runner->audioSystem);

    // Allocate the per-object instance list array once.
    // We don't need to reinitialize the list because the objt.count is fixed for this data.win.
    if (runner->instancesByObject == nullptr) {
        runner->instancesByObject = (Instance ***)safeCalloc(runner->dataWin->objt.count, sizeof(Instance**));
    }
    if (runner->instancesByExactObject == nullptr) {
        runner->instancesByExactObject = (Instance ***)safeCalloc(runner->dataWin->objt.count, sizeof(Instance**));
    }

    // Reset builtin function state
    runner->mpPotMaxrot = 30.0;
    runner->mpPotStep = 10.0;
    runner->mpPotAhead = 3.0;
    runner->mpPotOnSpot = true;
    runner->lastMusicInstance = -1;
#ifdef PLATFORM_VITA
    runner->vitaWeaponFingerprint = 0;
    runner->vitaArmorFingerprint = 0;
    runner->vitaEquipmentFingerprintStablePolls = 0;
    runner->vitaEquipmentFingerprintInitialized = false;
    runner->vitaLegacyTrophyMode = true;
    runner->vitaLegacyCreditsObjectIndex = -1;
    runner->vitaLegacyTrophyMask = 0;
    runner->vitaLegacyHpRoomIndex = -1;
    memset(runner->vitaLegacyHp, 0, sizeof(runner->vitaLegacyHp));
    runner->vitaLegacyHpStableFrames = 0;
    runner->vitaLegacyHpInitialized = false;
    runner->vitaLegacyStrengthFlag65 = 0;
    runner->vitaLegacyStrengthStableFrames = 0;
    runner->vitaLegacyStrengthInitialized = false;
    runner->vitaBattleBubbleObjectIndex = -1;
    repeat(runner->dataWin->objt.count, trophyObj) {
        const char* trophyObjName = runner->dataWin->objt.objects[trophyObj].name;
        if (trophyObjName == nullptr) continue;
        if (strcmp(trophyObjName, "obj_event_manager") == 0)
            runner->vitaLegacyTrophyMode = false;
        else if (strcmp(trophyObjName, "obj_credits") == 0)
            runner->vitaLegacyCreditsObjectIndex = (int32_t)trophyObj;
        else if (strcmp(trophyObjName, "obj_battleblcon") == 0)
            runner->vitaBattleBubbleObjectIndex = (int32_t)trophyObj;
    }
    if (runner->vitaLegacyTrophyMode)
        VitaProbe_logLine("TROPHY_BRIDGE=legacy_mode enabled=no_obj_event_manager");
#endif

    arrsetlen(runner->cachedDrawables, 0);
    runner->drawableListStructureDirty = true;
    runner->drawableListSortDirty = false;
}

static int compareTargetObjectIndexAscending(const void *a, const void *b) {
    FlattenedCollisionEvent* flat1 = (FlattenedCollisionEvent*) a;
    FlattenedCollisionEvent* flat2 = (FlattenedCollisionEvent*) b;
    if (flat1->targetObjectIndex > flat2->targetObjectIndex)
        return 1;
    else if (flat2->targetObjectIndex > flat1->targetObjectIndex)
        return -1;
    else return 0;
}

// Flattens collision-event inheritance into one list per object: Child-defined collision events override the parent's events
//
// (The YoYo Runner calls it "ExpandCollisionEvents")
static void flattenCollisionEvents(Runner* runner) {
    DataWin* dataWin = runner->dataWin;
    int32_t count = (int32_t) dataWin->objt.count;
    runner->flattenedCollisionEvents = (FlattenedCollisionEventList *)safeCalloc((size_t) (count > 0 ? count : 1), sizeof(FlattenedCollisionEventList));
    if (0 >= count) return;

    repeat(count, i) {
        GameObject* child = &dataWin->objt.objects[i];
        ObjectEventList* src = &child->eventLists[EVENT_COLLISION];
        FlattenedCollisionEventList* dst = &runner->flattenedCollisionEvents[i];

        if (src->eventCount > 0) {
            dst->events = (FlattenedCollisionEvent *)safeMalloc(src->eventCount * sizeof(FlattenedCollisionEvent));
            repeat(src->eventCount, e) {
                ObjectEvent* srcEvt = &src->events[e];
                int32_t srcCodeId = (srcEvt->actionCount > 0) ? srcEvt->actions[0].codeId : -1;
                FlattenedCollisionEvent fce = {0};
                fce.targetObjectIndex = srcEvt->eventSubtype;
                fce.codeId = srcCodeId;
                fce.ownerObjectIndex = i;
                dst->events[e] = fce;
            }
            dst->eventCount = src->eventCount;
        }

        int32_t ancestor = child->parentId;
        int32_t depth = 0;
        while (ancestor >= 0 && dataWin->objt.count > (uint32_t) ancestor && 32 > depth) {
            GameObject* anc = &dataWin->objt.objects[ancestor];
            ObjectEventList* ancList = &anc->eventLists[EVENT_COLLISION];
            repeat(ancList->eventCount, e) {
                ObjectEvent* ancEvt = &ancList->events[e];
                uint32_t target = ancEvt->eventSubtype;

                bool present = false;
                repeat(dst->eventCount, c) {
                    if (dst->events[c].targetObjectIndex == target) { present = true; break; }
                }
                if (present) continue;

                int32_t ancCodeId = (ancEvt->actionCount > 0) ? ancEvt->actions[0].codeId : -1;
                uint32_t newCount = dst->eventCount + 1;
                dst->events = (FlattenedCollisionEvent *)safeRealloc(dst->events, newCount * sizeof(FlattenedCollisionEvent));
                FlattenedCollisionEvent fce = {0};
                fce.targetObjectIndex = target;
                fce.codeId = ancCodeId;
                fce.ownerObjectIndex = ancestor;
                dst->events[newCount - 1] = fce;
                dst->eventCount = newCount;
            }
            ancestor = anc->parentId;
            depth++;
        }

        qsort(dst->events, dst->eventCount, sizeof(FlattenedCollisionEvent), compareTargetObjectIndexAscending);
    }
}

// Populates objectsWithAnyEventOfType[eventType] from the resolved event table: for each event type, the deduplicated list of concrete object indices that respond to ANY subtype of that event. Walks the inverted bySlot index per slot and dedups via a scratch byte set.
// Used by collision dispatch to skip non-collision objects in the outer loop, mirroring how the native obj_has_event table partitions instance iteration by event class.
static void populateObjectsWithAnyEventOfType(Runner* runner) {
    int32_t objectCount = (int32_t) runner->dataWin->objt.count;
    runner->objectsWithAnyEventOfType = (int32_t **)safeCalloc(OBJT_EVENT_TYPE_COUNT, sizeof(int32_t*));
    if (objectCount == 0) return;

    uint8_t* seen = (uint8_t *)safeCalloc((size_t) objectCount, 1);

    repeat(OBJT_EVENT_TYPE_COUNT, t) {
        int16_t* dense = runner->eventSlotMap.denseLookup[t];
        if (dense == nullptr) continue;
        int32_t maxSub = runner->eventSlotMap.maxSubtypeByType[t];
        memset(seen, 0, (size_t) objectCount);

        for (int32_t sub = 0; maxSub >= sub; sub++) {
            int32_t slot = dense[sub];
            if (0 > slot) continue;
            uint32_t entryCount;
            SlotResponderEntry* entries = ResolvedEventTable_slotEntries(&runner->eventTable, slot, &entryCount);
            repeat(entryCount, i) {
                int32_t obj = entries[i].concreteObjectId;
                if (obj < 0 || obj >= objectCount) continue;
                if (seen[obj]) continue;
                seen[obj] = 1;
                arrput(runner->objectsWithAnyEventOfType[t], obj);
            }
        }
    }

    free(seen);
}

// Validates if all required renderer functions are not null
static void validateRendererVtable(Renderer* renderer) {
    RendererVtable* v = (RendererVtable *)requireNotNull(renderer->vtable);

    #define requireNotNullFunction(fn) requireMessage(v->fn != nullptr, "Renderer " #fn " does not have a implementation!")
    requireNotNullFunction(init);
    requireNotNullFunction(destroy);
    requireNotNullFunction(beginFrame);
    requireNotNullFunction(endFrameInit);
    requireNotNullFunction(endFrameEnd);
    requireNotNullFunction(beginView);
    requireNotNullFunction(endView);
    requireNotNullFunction(beginGUI);
    requireNotNullFunction(setGuiProjection);
    requireNotNullFunction(endGUI);
    requireNotNullFunction(drawSprite);
    requireNotNullFunction(drawSpritePart);
    requireNotNullFunction(drawSpritePos);
    requireNotNullFunction(drawRectangle);
    requireNotNullFunction(drawRectangleColor);
    requireNotNullFunction(drawLine);
    requireNotNullFunction(drawTriangle);
    requireNotNullFunction(drawLineColor);
    requireNotNullFunction(drawText);
    requireNotNullFunction(drawTextColor);
    requireNotNullFunction(drawSpriteTiled);
    requireNotNullFunction(drawSurfaceTiled);
    requireNotNullFunction(flush);
    requireNotNullFunction(clearScreen);
    requireNotNullFunction(createSpriteFromSurface);
    requireNotNullFunction(deleteSprite);
    requireNotNullFunction(gpuSetBlendMode);
    requireNotNullFunction(gpuSetBlendModeExt);
    requireNotNullFunction(gpuSetBlendEnable);
    requireNotNullFunction(gpuGetBlendEnable);
    requireNotNullFunction(gpuSetAlphaTestEnable);
    requireNotNullFunction(gpuSetAlphaTestRef);
    requireNotNullFunction(gpuSetColorWriteEnable);
    requireNotNullFunction(gpuGetColorWriteEnable);
    requireNotNullFunction(createSurface);
    requireNotNullFunction(surfaceExists);
    requireNotNullFunction(setRenderTarget);
    requireNotNullFunction(ensureApplicationSurface);
    requireNotNullFunction(getSurfaceWidth);
    requireNotNullFunction(getSurfaceHeight);
    requireNotNullFunction(drawSurface);
    requireNotNullFunction(surfaceResize);
    requireNotNullFunction(surfaceFree);
    requireNotNullFunction(surfaceCopy);
    requireNotNullFunction(surfaceGetPixels);
    requireNotNullFunction(spriteGetTexture);
    requireNotNullFunction(surfaceGetTexture);
    requireNotNullFunction(textureGetTexelWidth);
    requireNotNullFunction(textureGetTexelHeight);
    requireNotNullFunction(textureGetUVs);
    requireNotNullFunction(textureSetStage);
    requireNotNullFunction(gpuSetShader);
    requireNotNullFunction(gpuResetShader);
    requireNotNullFunction(shaderGetUniform);
    requireNotNullFunction(shaderGetSamplerIndex);
    requireNotNullFunction(shaderSetUniformF);
    requireNotNullFunction(shaderSetUniformI);
    requireNotNullFunction(shaderIsCompiled);
    requireNotNullFunction(shadersSupported);
    #undef requireNotNullFunction
}

Runner* Runner_create(DataWin* dataWin, VMContext* vm, Renderer* renderer, FileSystem* fileSystem, AudioSystem* audioSystem) {
    requireNotNull(dataWin);
    requireNotNull(vm);
    requireNotNull(renderer);
    requireNotNull(fileSystem);
    requireNotNull(audioSystem);
    validateRendererVtable(renderer);

    Runner* runner = (Runner *)safeCalloc(1, sizeof(Runner));
    runner->dataWin = dataWin;
    runner->vmContext = vm;
    runner->renderer = renderer;
    runner->fileSystem = fileSystem;
    runner->audioSystem = audioSystem;
    runner->frameCount = 0;
    runner->osType = OS_WINDOWS;
    runner->keyboard = RunnerKeyboard_create();
    runner->gamepads = RunnerGamepad_create();
    runner->mouse = RunnerMouse_create();
    runner->appSurfaceEnabled = true;
    runner->windowTitle = dataWin->gen8.displayName ? safeStrdup(dataWin->gen8.displayName) : nullptr;
    runner->appSurfaceAutoDraw = true;
    runner->usingAppSurface = true;
    runner->applicationWidth = (int32_t) dataWin->gen8.defaultWindowWidth;
    runner->applicationHeight = (int32_t) dataWin->gen8.defaultWindowHeight;
    runner->oldApplicationWidth = runner->applicationWidth;
    runner->oldApplicationHeight = runner->applicationHeight;
    runner->widescreenExtraWidth = 0;
    runner->widescreenExtraHeight = 0;
    runner->freeCamPanX = 0.0f;
    runner->freeCamPanY = 0.0f;
    runner->freeCamZoom = 1.0f;
    runner->applicationSurfaceId = APPLICATION_SURFACE_ID;
    renderer->runner = runner;
    runner->viewportW = 1;
    runner->viewportH = 1;

    repeat(MAX_SURFACES, i) {
        runner->surfaceStack[i] = -1;
    }

    // Collision compatibility mode is "enabled" for all pre-GM 2022.1 games AND for any post-GM 2022.1 games that have the bit 27 set
    bool isVersionAtLeastGM_2022_1 = DataWin_isVersionAtLeast(dataWin, 2022, 1, 0, 0);
    runner->collisionCompatibilityMode = !isVersionAtLeastGM_2022_1 || ((dataWin->optn.info >> 27) & 1) != 0;

    // Build the event dispatch acceleration tables.
    EventSlotMap_build(&runner->eventSlotMap, dataWin);
    ResolvedEventTable_build(&runner->eventTable, dataWin, &runner->eventSlotMap);
    flattenCollisionEvents(runner);

    // Create assets map
    shdefault(runner->assetsByName, -1);
    repeat(dataWin->objt.count, i) {
        if (!dataWin->objt.objects[i].present) continue;
        shput(runner->assetsByName, dataWin->objt.objects[i].name, i);
    }
    repeat(dataWin->sprt.count, i) {
        if (!dataWin->sprt.sprites[i].present) continue;
        shput(runner->assetsByName, dataWin->sprt.sprites[i].name, i);
    }
    repeat(dataWin->sond.count, i) {
        if (!dataWin->sond.sounds[i].present) continue;
        shput(runner->assetsByName, dataWin->sond.sounds[i].name, i);
    }
    repeat(dataWin->bgnd.count, i) {
        if (!dataWin->bgnd.backgrounds[i].present) continue;
        shput(runner->assetsByName, dataWin->bgnd.backgrounds[i].name, i);
    }
    repeat(dataWin->path.count, i) {
        if (!dataWin->path.paths[i].present) continue;
        shput(runner->assetsByName, dataWin->path.paths[i].name, i);
    }
    repeat(dataWin->scpt.count, i) {
        if (!dataWin->scpt.scripts[i].present) continue;
        shput(runner->assetsByName, dataWin->scpt.scripts[i].name, i);
    }
    repeat(dataWin->font.count, i) {
        if (!dataWin->font.fonts[i].present) continue;
        shput(runner->assetsByName, dataWin->font.fonts[i].name, i);
    }
    repeat(dataWin->tmln.count, i) {
        if (!dataWin->tmln.timelines[i].present) continue;
        shput(runner->assetsByName, dataWin->tmln.timelines[i].name, i);
    }
    repeat(dataWin->room.count, i) {
        if (!dataWin->room.rooms[i].present) continue;
        shput(runner->assetsByName, dataWin->room.rooms[i].name, i);
    }

    repeat(shlen(vm->builtinMap), i) {
        bool isRegistered = shgeti(vm->codeIndexByName, vm->builtinMap[i].key) != -1;
        if (isRegistered) {
            fprintf(stderr, "Runner: Builtin function %s has the same name as a GML script! The script may be a compatibility script provided by GM:S 2+, and the game may have issues due to the builtin overriding it!\n", vm->builtinMap[i].key);
        }
    }

    Runner_reset(runner);

    populateObjectsWithAnyEventOfType(runner);

    // Link runner to VM context
    vm->runner = (struct Runner*) runner;

#ifdef PLATFORM_VITA
    // OpenAL's Vita backend creates its device/context and converts the initial
    // hardware buffers. Doing this after the chapter renderer has committed its
    // allocations can expose an SceGxm data abort on larger translated data.win
    // files. Audio has no dependency on renderer initialization, so reserve its
    // resources first and then initialize the renderer.
    fprintf(stderr, "RUNNER_INIT=audio_begin\n");
    audioSystem->vtable->init(audioSystem, dataWin, fileSystem);
    fprintf(stderr, "RUNNER_INIT=audio_complete\n");
    fprintf(stderr, "RUNNER_INIT=renderer_begin\n");
    renderer->vtable->init(renderer, dataWin);
    fprintf(stderr, "RUNNER_INIT=renderer_complete\n");
#else
    renderer->vtable->init(renderer, dataWin);
    audioSystem->vtable->init(audioSystem, dataWin, fileSystem);
#endif

    return runner;
}

static inline void dispatchInstanceCreationEvents(Runner* runner, Instance* inst, Instance* variables) {
    inst->createEventFired = true;
    Runner_executeEvent(runner, inst, EVENT_PRECREATE, 0);
    if (inst->destroyed) return;
    VMBuiltins_applyCreationVariables(runner->vmContext, inst, variables);
    Runner_executeEvent(runner, inst, EVENT_CREATE, 0);
}

Instance* Runner_createStruct(Runner* runner) {
    Instance* s = Instance_create(runner->nextInstanceId++, STRUCT_OBJECT_INDEX, 0, 0);
    hmput(runner->instancesById, s->instanceId, s);
    s->structRegistryIndex = (int32_t) arrlen(runner->structInstances);
    arrput(runner->structInstances, s);
    s->refCount = 1;
    return s;
}

Instance* Runner_createInstance(Runner* runner, GMLReal x, GMLReal y, int32_t objectIndex) {
    if (isObjectDisabled(runner, objectIndex)) return nullptr;
    Instance* inst = createAndInitInstance(runner, runner->nextInstanceId++, objectIndex, x, y);
    dispatchInstanceCreationEvents(runner, inst, nullptr);
    return inst;
}

// Same as Runner_createInstance, but sets depth BEFORE firing Create events so scripts like scr_depth can override.
Instance* Runner_createInstanceWithDepth(Runner* runner, GMLReal x, GMLReal y, int32_t objectIndex, int32_t depth) {
    return Runner_createInstanceWithDepthVars(runner, x, y, objectIndex, depth, nullptr);
}

Instance* Runner_createInstanceWithDepthVars(Runner* runner, GMLReal x, GMLReal y, int32_t objectIndex, int32_t depth, Instance* variables) {
    if (isObjectDisabled(runner, objectIndex)) return nullptr;
    Instance* inst = createAndInitInstance(runner, runner->nextInstanceId++, objectIndex, x, y);
    inst->depth = depth;
    dispatchInstanceCreationEvents(runner, inst, variables);
    return inst;
}

Instance* Runner_createInstanceWithLayer(Runner* runner, GMLReal x, GMLReal y, int32_t objectIndex, int32_t layerId) {
    return Runner_createInstanceWithLayerVars(runner, x, y, objectIndex, layerId, nullptr);
}

Instance* Runner_createInstanceWithLayerVars(Runner* runner, GMLReal x, GMLReal y, int32_t objectIndex, int32_t layerId, Instance* variables) {
    if (isObjectDisabled(runner, objectIndex)) return nullptr;
    RuntimeLayer* rl = Runner_findRuntimeLayerById(runner, layerId);
    if (rl == nullptr) {
        fprintf(stderr, "Runner: instance_create_layer: Layer ID %d not found!\n", layerId);
        return nullptr;
    }
    Instance* inst = createAndInitInstance(runner, runner->nextInstanceId++, objectIndex, x, y);
    inst->layer = layerId;
    inst->depth = rl->depth;
    Runner_addInstanceLayerElement(runner, layerId, inst->instanceId);
    dispatchInstanceCreationEvents(runner, inst, variables);
    return inst;
}

Instance* Runner_copyInstance(Runner* runner, Instance* source, bool performEvent) {
    requireNotNull(source);
    if (isObjectDisabled(runner, source->objectIndex)) return nullptr;

    Instance* inst = createAndInitInstance(runner, runner->nextInstanceId++, source->objectIndex, source->x, source->y);
    Instance_copyFields(inst, source);
    inst->createEventFired = true;
    if (performEvent) {
        Runner_executeEvent(runner, inst, EVENT_PRECREATE, 0);
        Runner_executeEvent(runner, inst, EVENT_CREATE, 0);
    }
    return inst;
}

void Runner_setGameArgs(Runner* runner, char** argv, int32_t argc) {
    repeat(arrlen(runner->gameArgs), i) free(runner->gameArgs[i]);
    arrfree(runner->gameArgs);
    runner->gameArgs = nullptr;
    repeat(argc, i) arrput(runner->gameArgs, safeStrdup(argv[i]));
}

void Runner_destroyInstance(MAYBE_UNUSED Runner* runner, Instance* inst, bool runDestroyEvent) {
    // We check this to avoid a infinite loop if "inst" is destroyed within a event destroy event
    if (inst->destroyed)
        return;
    inst->destroyed = true;
    if (runDestroyEvent)
        Runner_executeEvent(runner, inst, EVENT_DESTROY, 0);
    Runner_executeEvent(runner, inst, EVENT_CLEANUP, 0);
    // A destroyed instance must ALWAYS be not active
    // If a destroyed instance is active, then well, something went VERY wrong
    inst->active = false;
    Runner_removeInstanceFromObjectLists(runner, inst);

#ifdef PLATFORM_VITA
    if (runner != nullptr && runner->dataWin != nullptr &&
        inst->objectIndex >= 0 &&
        inst->objectIndex < (int32_t) runner->dataWin->objt.count) {
        const char* objectName = runner->dataWin->objt.objects[inst->objectIndex].name;
        if (objectName != nullptr && strstr(objectName, "mouse") != nullptr) {
            char line[192];
            snprintf(line, sizeof(line),
                     "MICE destroy object=%s instance=%d removed=1 active=%d",
                     objectName, inst->instanceId, inst->active ? 1 : 0);
            extern void VitaProbe_logLine(const char* text);
            VitaProbe_logLine(line);
        }
    }
#endif

#ifdef ENABLE_VM_TRACING
    GameObject* gameObject = &runner->dataWin->objt.objects[inst->objectIndex];
    if (shgeti(runner->vmContext->instanceLifecyclesToBeTraced, "*") != -1 || shgeti(runner->vmContext->instanceLifecyclesToBeTraced, gameObject->name) != -1) {
        fprintf(stderr, "VM: Instance %s (instanceId=%d,objectIndex=%d) destroyed\n", gameObject->name, inst->instanceId, inst->objectIndex);
    }
#endif
}

RuntimeLayer* Runner_findRuntimeLayerByName(Runner* runner, char* name) {
    size_t count = arrlenu(runner->runtimeLayers);
    repeat(count, i) {
        if (strcmp(runner->runtimeLayers[i].dynamicName, name) == 0)
            return &runner->runtimeLayers[i];
    }
    return nullptr;
}

RuntimeLayer* Runner_findRuntimeLayerById(Runner* runner, int32_t id) {
    size_t count = arrlenu(runner->runtimeLayers);
    repeat(count, i) {
        if ((int32_t) runner->runtimeLayers[i].id == id)
            return &runner->runtimeLayers[i];
    }
    return nullptr;
}

RoomLayer* Runner_findRoomLayerById(Room* room, int32_t id) {
    requireNotNull(room);
    repeat(room->layerCount, i) {
        if ((int32_t) room->layers[i].id == id) return &room->layers[i];
    }
    return nullptr;
}

RuntimeLayerElement* Runner_findLayerElementById(Runner* runner, int32_t elementId, RuntimeLayer** outLayer) {
    size_t layerCount = arrlenu(runner->runtimeLayers);
    repeat(layerCount, i) {
        RuntimeLayer* runtimeLayer = &runner->runtimeLayers[i];
        size_t elementCount = arrlenu(runtimeLayer->elements);
        repeat(elementCount, j) {
            if ((int32_t) runtimeLayer->elements[j].id == elementId) {
                if (outLayer != nullptr)
                    *outLayer = runtimeLayer;

                return &runtimeLayer->elements[j];
            }
        }
    }
    if (outLayer != nullptr) *outLayer = nullptr;
    return nullptr;
}

uint32_t Runner_getNextLayerId(Runner* runner) {
    return runner->nextLayerId++;
}

void Runner_addInstanceLayerElement(Runner* runner, int32_t layerId, int32_t instanceId) {
    RuntimeLayer* runtimeLayer = Runner_findRuntimeLayerById(runner, layerId);
    if (runtimeLayer == nullptr) return;
    RuntimeLayerElement el = {0};
    el.id = Runner_getNextLayerId(runner);
    el.type = RuntimeLayerElementType_Instance;
    el.visible = true;
    el.alpha = 1.0f;
    el.blend = 0xFFFFFF;
    el.instanceId = instanceId;
    arrput(runtimeLayer->elements, el);
}

void Runner_removeInstanceLayerElement(Runner* runner, int32_t instanceId) {
    size_t layerCount = arrlenu(runner->runtimeLayers);
    repeat(layerCount, i) {
        RuntimeLayer* runtimeLayer = &runner->runtimeLayers[i];
        size_t elementCount = arrlenu(runtimeLayer->elements);
        repeat(elementCount, j) {
            RuntimeLayerElement* el = &runtimeLayer->elements[j];
            if (el->type == RuntimeLayerElementType_Instance && el->instanceId == instanceId) {
                arrdel(runtimeLayer->elements, j);
                return;
            }
        }
    }
}

// Reaps GML structs whose only remaining ref is the structInstances registry's implicit +1.
// Walks backward so that swap-remove of dead entries doesn't disturb the indexes of entries we haven't visited yet.
static void Runner_sweepDeadStructs(Runner* runner) {
    int32_t count = (int32_t) arrlen(runner->structInstances);
    for (int32_t i = count - 1; i >= 0; i--) {
        Instance* s = runner->structInstances[i];
        if (s->refCount > 1) continue; // still referenced by user code
        if (s->pinned) continue; // Don't sweep pinned structs
        require(s->refCount == 1);

        // Remove from runner->instancesById so future findInstanceByTarget(id) returns nullptr.
        hmdel(runner->instancesById, s->instanceId);

        // O(1) swap-remove from structInstances, keeping structRegistryIndex in sync.
        int32_t lastIdx = (int32_t) arrlen(runner->structInstances) - 1;
        if (i != lastIdx) {
            Instance* moved = runner->structInstances[lastIdx];
            runner->structInstances[i] = moved;
            moved->structRegistryIndex = i;
        }
        arrpop(runner->structInstances);

        s->structRegistryIndex = -1;
        s->refCount = 0; // drop the registry's ref; we are about to free
        Instance_free(s);
    }
}

void Runner_cleanupDestroyedInstances(Runner* runner) {
    int32_t count = (int32_t) arrlen(runner->instances);
    int32_t writeIdx = 0;
    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (!inst->destroyed) {
            runner->instances[writeIdx++] = inst;
        } else {
            Runner_removeInstanceFromObjectLists(runner, inst);
            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            Runner_removeInstanceLayerElement(runner, inst->instanceId);
            hmdel(runner->instancesById, inst->instanceId);
            Instance_free(inst);
            // Cached drawables hold raw Instance* that we just freed; force a rebuild before the next draw.
            runner->drawableListStructureDirty = true;
        }
    }
    arrsetlen(runner->instances, writeIdx);
}

void Runner_initFirstRoom(Runner* runner) {
    DataWin* dataWin = runner->dataWin;
    require(dataWin->gen8.roomOrderCount > 0);

    int32_t firstRoomIndex = dataWin->gen8.roomOrder[0];

    runner->gameStartTime = nowNanos();

    // GameMaker Studio (as of 2024.14) applies an offset of 1 virtual pixel to certain
    // primitives if bit 35 is set.  Earlier versions of GameMaker or GameMaker Studio
    // appear to apply it unconditionally.
    runner->applyOffsetForPrimitives = !DataWin_isVersionAtLeast(dataWin, 2024, 14, 0, 0) || ((dataWin->optn.info >> 35) & 1);

    // Run global init scripts with the global scope instance as "self"
    // In GMS 2.3+ (BC17), GLOB scripts store function declarations on "self" via Pop.v.v
    runner->vmContext->currentInstance = runner->vmContext->globalScopeInstance;
    repeat(dataWin->glob.count, i) {
        int32_t codeId = dataWin->glob.codeIds[i];
        if (codeId >= 0 && dataWin->code.count > (uint32_t) codeId) {
            fprintf(stderr, "Runner: Executing global init script: %s\n", dataWin->code.entries[codeId].name);
            RValue result = VM_executeCode(runner->vmContext, codeId);
            RValue_free(&result);
        }
    }

    // Run extension init scripts
    runner->vmContext->currentInstance =  runner->vmContext->globalScopeInstance;
    repeat(dataWin->extn.count, e) {
        Extension* ext = &dataWin->extn.extensions[e];
        repeat(ext->fileCount, f) {
            const char* initScript = ext->files[f].initScript;
            if (initScript == nullptr || initScript[0] == '\0') continue;
            int32_t scriptIndex = shget(runner->assetsByName, initScript);
            if (0 > scriptIndex || (uint32_t) scriptIndex >= dataWin->scpt.count) {
                fprintf(stderr, "Runner: Extension init script '%s' not found, skipping\n", initScript);
                continue;
            }
            int32_t codeId = dataWin->scpt.scripts[scriptIndex].codeId;
            if (codeId >= 0 && dataWin->code.count > (uint32_t) codeId) {
                fprintf(stderr, "Runner: Executing extension init script: %s\n", initScript);
                RValue result = VM_executeCode(runner->vmContext, codeId);
                RValue_free(&result);
            }
        }
    }
    runner->vmContext->currentInstance = nullptr;

    // Initialize the first room
    initRoom(runner, firstRoomIndex);

    // Fire Game Start for all instances
    Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_GAME_START);
    runner->gameStartFired = true;

    // Fire Room Start for all instances
    Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ROOM_START);
}

// ===[ Collision Event Dispatch ]===

#ifdef ENABLE_VM_TRACING
// Returns true if this collision pair should be logged under --trace-collisions. Matches "*" or either side's object name.
static bool shouldTraceCollisionPair(VMContext* vm, DataWin* dataWin, Instance* a, Instance* b) {
    if (shlen(vm->collisionsToBeTraced) == -1) return false;
    if (shgeti(vm->collisionsToBeTraced, "*") != -1) return true;
    const char* aName = dataWin->objt.objects[a->objectIndex].name;
    const char* bName = dataWin->objt.objects[b->objectIndex].name;
    if (aName && shgeti(vm->collisionsToBeTraced, aName) != -1) return true;
    if (bName && shgeti(vm->collisionsToBeTraced, bName) != -1) return true;
    return false;
}
#endif

// Finds if the "instance" has a collision event handler for "collisionMatch"
// Returns nullptr if no match (instance has no collision handler that applies to collisionMatch).
static FlattenedCollisionEvent* findSymmetricCollisionEvent(Runner* runner, Instance* instance, Instance* collisionMatch) {
    DataWin* dataWin = runner->dataWin;
    FlattenedCollisionEventList* list = &runner->flattenedCollisionEvents[instance->objectIndex];
    if (list->eventCount == 0)
        return nullptr;

    int32_t partnerObj = collisionMatch->objectIndex;
    int32_t depth = 0;
    while (partnerObj >= 0 && dataWin->objt.count > (uint32_t) partnerObj && 32 > depth) {
        repeat(list->eventCount, e) {
            FlattenedCollisionEvent* evt = &list->events[e];
            if ((int32_t) evt->targetObjectIndex == partnerObj) {
                if (0 > evt->codeId)
                    return nullptr;

                return evt;
            }
        }
        partnerObj = dataWin->objt.objects[partnerObj].parentId;
        depth++;
    }

    return nullptr;
}

static void executeCollisionEvent(Runner* runner, Instance* self, Instance* other, int32_t targetObjectIndex, int32_t codeId, int32_t ownerObjectIndex) {
    if (isEventBlockedByPendingRoom(runner, self, EVENT_COLLISION))
        return;

    VMContext* vm = runner->vmContext;

    // Save event context
    int32_t savedEventType = vm->currentEventType;
    int32_t savedEventSubtype = vm->currentEventSubtype;
    int32_t savedEventObjectIndex = vm->currentEventObjectIndex;
    struct Instance* savedOtherInstance = vm->otherInstance;

    // Set collision event context
    vm->currentEventType = EVENT_COLLISION;
    vm->currentEventSubtype = targetObjectIndex;
    vm->otherInstance = other;
    vm->currentEventObjectIndex = ownerObjectIndex;

#ifdef ENABLE_VM_TRACING
    if (codeId >= 0 && shlen(vm->eventsToBeTraced) != -1) {
        const char* selfName = runner->dataWin->objt.objects[self->objectIndex].name;
        const char* targetName = runner->dataWin->objt.objects[targetObjectIndex].name;
        bool shouldTrace = shgeti(vm->eventsToBeTraced, "*") != -1 || shgeti(vm->eventsToBeTraced, "Collision") != -1 || shgeti(vm->eventsToBeTraced, selfName) != -1;
        if (shouldTrace) {
            fprintf(stderr, "Runner: [%s] Collision with %s (instanceId=%d, otherId=%d)\n", selfName, targetName, self->instanceId, other->instanceId);
        }
    }
#endif

    executeCode(runner, self, codeId);

    // Restore event context
    vm->currentEventType = savedEventType;
    vm->currentEventSubtype = savedEventSubtype;
    vm->currentEventObjectIndex = savedEventObjectIndex;
    vm->otherInstance = savedOtherInstance;
}

// ===[ Path Adaptation ]===
// Advances path position and updates instance x/y (HTML5: yyInstance.js Adapt_Path, lines 2755-2881)
// Returns true if end of path was reached (and pathSpeed != 0), to fire OTHER_END_OF_PATH event.
static bool adaptPath(Runner* runner, Instance* inst) {
    if (0 > inst->pathIndex) return false;

    DataWin* dataWin = runner->dataWin;
    if ((uint32_t) inst->pathIndex >= dataWin->path.count) return false;

    GamePath* path = &dataWin->path.paths[inst->pathIndex];
    if (0.0 >= path->length) return false;

    bool atPathEnd = false;

    GMLReal orient = inst->pathOrientation * M_PI / 180.0;

    // Get current position's speed factor
    PathPositionResult cur = GamePath_getPosition(path, inst->pathPosition);
    GMLReal sp = cur.speed / (100.0 * inst->pathScale);

    // Advance position (compute in higher precision, truncate to float on store - matches native runner)
    inst->pathPosition = (float) (inst->pathPosition + inst->pathSpeed * sp / path->length);

    // Handle end actions if position out of [0,1]
    PathPositionResult pos0 = GamePath_getPosition(path, 0.0f);
    if (inst->pathPosition >= 1.0f || 0.0f >= inst->pathPosition) {
        atPathEnd = (inst->pathSpeed == 0.0f) ? false : true;

        switch (inst->pathEndAction) {
            // stop moving
            case 0: {
                if (inst->pathSpeed >= 0.0f) {
                    if (inst->pathSpeed != 0.0f) {
                        inst->pathPosition = 1.0f;
                        inst->pathIndex = -1;
                    }
                } else {
                    inst->pathPosition = 0.0f;
                    inst->pathIndex = -1;
                }
                break;
            }
            // continue from start position (restart)
            case 1: {
                if (0.0f > inst->pathPosition) {
                    inst->pathPosition += 1.0f;
                } else {
                    inst->pathPosition -= 1.0f;
                }
                break;
            }
            // continue from current position
            case 2: {
                PathPositionResult pos1 = GamePath_getPosition(path, 1.0f);
                GMLReal xx = pos1.x - pos0.x;
                GMLReal yy = pos1.y - pos0.y;
                GMLReal xdif = inst->pathScale * (xx * GMLReal_cos(orient) + yy * GMLReal_sin(orient));
                GMLReal ydif = inst->pathScale * (yy * GMLReal_cos(orient) - xx * GMLReal_sin(orient));

                if (0.0f > inst->pathPosition) {
                    inst->pathXStart -= (float) xdif;
                    inst->pathYStart -= (float) ydif;
                    inst->pathPosition += 1.0f;
                } else {
                    inst->pathXStart += (float) xdif;
                    inst->pathYStart += (float) ydif;
                    inst->pathPosition -= 1.0f;
                }
                break;
            }
            // reverse
            case 3: {
                if (0.0f > inst->pathPosition) {
                    inst->pathPosition = -inst->pathPosition;
                    inst->pathSpeed = (float) GMLReal_fabs(inst->pathSpeed);
                } else {
                    inst->pathPosition = 2.0f - inst->pathPosition;
                    inst->pathSpeed = (float) -GMLReal_fabs(inst->pathSpeed);
                }
                break;
            }
            // default: stop
            default: {
                inst->pathPosition = 1.0f;
                inst->pathIndex = -1;
                break;
            }
        }
    }

    // Find the new position in the room
    PathPositionResult newPos = GamePath_getPosition(path, inst->pathPosition);
    GMLReal xx = newPos.x - pos0.x; // relative
    GMLReal yy = newPos.y - pos0.y;

    GMLReal newx = inst->pathXStart + inst->pathScale * (xx * GMLReal_cos(orient) + yy * GMLReal_sin(orient));
    GMLReal newy = inst->pathYStart + inst->pathScale * (yy * GMLReal_cos(orient) - xx * GMLReal_sin(orient));

    // Trick to set the direction: set hspeed/vspeed to delta, which updates direction
    inst->hspeed = (float) (newx - inst->x);
    inst->vspeed = (float) (newy - inst->y);
    Instance_computeSpeedFromComponents(inst);

    // Normal speed should not be used
    inst->speed = 0.0f;
    inst->hspeed = 0.0f;
    inst->vspeed = 0.0f;

    // Set the new position
    inst->x = (float) newx;
    inst->y = (float) newy;

    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);

    return atPathEnd;
}

void Runner_updateMousePosition(Runner* runner, int32_t windowWidth, int32_t windowHeight, double mouseXInWindow, double mouseYInWindow) {
    if (windowWidth <= 0 || windowHeight <= 0 || runner->currentRoom == nullptr) return;

    uint32_t gameW = runner->renderGameW > 0 ? runner->renderGameW : runner->currentRoom->width;
    uint32_t gameH = runner->renderGameH > 0 ? runner->renderGameH : runner->currentRoom->height;

    double fboX = ((mouseXInWindow - runner->viewportX) / runner->viewportW) * gameW;
    double fboY = ((mouseYInWindow - runner->viewportY) / runner->viewportH) * gameH;

    runner->mouse->screenX = fboX;
    runner->mouse->screenY = fboY;

    // GUI space is just the FBO normalized to [0,1] (no camera involved), consumed by device_mouse_*_to_gui.
    runner->mouse->normalizedX = gameW > 0 ? fboX / gameW : (double) 0.0;
    runner->mouse->normalizedY = gameH > 0 ? fboY / gameH : (double) 0.0;
}

void Runner_getMouseRoomPosition(Runner* runner, GMLReal* outX, GMLReal* outY) {
    double fboX = runner->mouse->screenX;
    double fboY = runner->mouse->screenY;

    if (runner->currentRoom == nullptr) {
        *outX = (GMLReal) fboX;
        *outY = (GMLReal) fboY;
        return;
    }

    int32_t gameW = runner->renderGameW > 0 ? runner->renderGameW : runner->currentRoom->width;
    int32_t gameH = runner->renderGameH > 0 ? runner->renderGameH : runner->currentRoom->height;

    // The renderer scales every view port from app-surface space into FBO space by this factor (see Runner_drawViews).
    // The cursor (fboX/fboY) is already in FBO space, so the port rects we compare/divide by must be scaled the same way,
    // otherwise the mapping is wrong whenever displayScale != 1 (e.g. the widescreen hack widens the FBO but not the ports).
    float displayScaleX, displayScaleY;
    Runner_computeViewDisplayScale(runner, gameW, gameH, &displayScaleX, &displayScaleY);

    // Find the view whose (FBO-space) port rect contains the cursor; fall back to the first enabled view, then to a default mapping when no views are enabled.
    // Native runner rule (GR_Window_Views_Convert): count enabled views that render directly to screen (view_surface_id == -1).
    // If any exist, map via the one whose port contains the cursor (or fall through to the last one tried).
    // If ALL enabled views have a surface bound, use room-space mapping, since the game is manually compositing those surfaces onto the window.
    bool viewsEnabled = runner->viewsEnabled;
    int32_t screenViewCount = 0;
    int32_t pickedViewIndex = -1;
    int32_t lastScreenViewIndex = -1;
    if (viewsEnabled) {
        repeat(MAX_VIEWS, vi) {
            RuntimeView* v = &runner->views[vi];
            if (!v->enabled || v->surfaceId != -1) continue;
            screenViewCount++;
            lastScreenViewIndex = (int32_t) vi;
            int32_t portX = (int32_t) ((float) v->portX * displayScaleX + 0.5f);
            int32_t portY = (int32_t) ((float) v->portY * displayScaleY + 0.5f);
            int32_t portW = (int32_t) ((float) v->portWidth * displayScaleX + 0.5f);
            int32_t portH = (int32_t) ((float) v->portHeight * displayScaleY + 0.5f);
            if (fboX >= portX && fboX < portX + portW && fboY >= portY && fboY < portY + portH) {
                pickedViewIndex = (int32_t) vi;
                break;
            }
        }
        if (pickedViewIndex < 0) pickedViewIndex = lastScreenViewIndex;
    }

    // The port (screen rect) stays on the view, but the world rect comes from the view's assigned camera (it scrolls as the camera follows its target).
    RuntimeView* pickedView = (pickedViewIndex >= 0) ? &runner->views[pickedViewIndex] : nullptr;
    GMLCamera* pickedCamera = (pickedViewIndex >= 0) ? Runner_getCameraForView(runner, pickedViewIndex) : nullptr;

    if (pickedView != nullptr && pickedCamera != nullptr && pickedView->portWidth > 0 && pickedView->portHeight > 0 && pickedCamera->viewWidth > 0 && pickedCamera->viewHeight > 0) {
        // Apply the SAME widescreen view expansion the renderer uses in Runner_drawViews, so the inverse mapping matches the pixels actually drawn.
        int32_t widescreenBaseW = gameW - runner->widescreenExtraWidth;
        int32_t widescreenBaseH = gameH - runner->widescreenExtraHeight;
        int32_t viewX, viewY, viewW, viewH;
        expandViewAxis(pickedCamera->viewX, pickedCamera->viewWidth, gameW, widescreenBaseW, &viewX, &viewW);
        expandViewAxis(pickedCamera->viewY, pickedCamera->viewHeight, gameH, widescreenBaseH, &viewY, &viewH);

        // Scale the picked view's port into FBO space exactly as Runner_drawViews does.
        int32_t portX = (int32_t) ((float) pickedView->portX * displayScaleX + 0.5f);
        int32_t portY = (int32_t) ((float) pickedView->portY * displayScaleY + 0.5f);
        int32_t portW = (int32_t) ((float) pickedView->portWidth * displayScaleX + 0.5f);
        int32_t portH = (int32_t) ((float) pickedView->portHeight * displayScaleY + 0.5f);

        // Map the cursor through the inverse of the view's world->clip projection (the same matrix the renderer draws with): cursor -> NDC within the port -> world.
        Matrix4f worldToClip, clipToWorld;
        Matrix4f_viewProjection(&worldToClip, (float) viewX, (float) viewY, (float) viewW, (float) viewH, pickedCamera->viewAngle);
        Matrix4f_inverse(&clipToWorld, &worldToClip);

        float ndcX = (float) ((fboX - portX) / portW) * 2.0f - 1.0f;
        float ndcY = 1.0f - (float) ((fboY - portY) / portH) * 2.0f;
        float worldX, worldY;
        Matrix4f_transformPoint(&clipToWorld, ndcX, ndcY, &worldX, &worldY);
        *outX = (GMLReal) worldX;
        *outY = (GMLReal) worldY;
    } else if (viewsEnabled && screenViewCount == 0) {
        // No enabled view renders to screen (all redirect to surfaces). Mouse is in room space.
        *outX = (GMLReal) (runner->mouse->normalizedX * runner->currentRoom->width);
        *outY = (GMLReal) (runner->mouse->normalizedY * runner->currentRoom->height);
    } else {
        // No views enabled: the renderer maps the FBO directly to world, shifted by the widescreen origin offset (matches the fullscreen path in Runner_drawViews).
        *outX = (GMLReal) (fboX - runner->widescreenExtraWidth / 2);
        *outY = (GMLReal) (fboY - runner->widescreenExtraHeight / 2);
    }
}

// Fires one local mouse subtype event on every instance in a snapshot that currently has the mouse over it.
// mouseIsOver must already have been computed for each instance.
static void fireLocalMouseSubtype(Runner* runner, int32_t subtype, int32_t slot, Instance** snapshot, int32_t count, bool* isOverArr) {
    if (slot < 0) return;
    ResolvedEventTable* table = &runner->eventTable;
    repeat(count, i) {
        Instance* inst = snapshot[i];
        if (!inst->active) continue;
        if (!isOverArr[i]) continue;
        int32_t ownerObjectIndex = -1;
        int32_t codeId = ResolvedEventTable_lookup(table, inst->objectIndex, slot, &ownerObjectIndex);
        if (0 > codeId) continue;
        Runner_executeResolvedEvent(runner, inst, EVENT_MOUSE, subtype, codeId, ownerObjectIndex);
        if (runner->pendingRoom >= 0) return;
    }
}

static void dispatchMouseEvents(Runner* runner) {
    RunnerMouseState* mouse = runner->mouse;
    DataWin* dataWin = runner->dataWin;

    GMLReal mx, my;
    Runner_getMouseRoomPosition(runner, &mx, &my);

    // ---[ Global events: fire for all objects regardless of mouse position ]---

    // Global button held (50-52)
    if (RunnerMouse_checkButton(mouse, GML_MB_LEFT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_LEFT_BUTTON);
    if (RunnerMouse_checkButton(mouse, GML_MB_RIGHT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_RIGHT_BUTTON);
    if (RunnerMouse_checkButton(mouse, GML_MB_MIDDLE))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_MIDDLE_BUTTON);

    // Global button pressed (53-55)
    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_LEFT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_LEFT_PRESSED);
    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_RIGHT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_RIGHT_PRESSED);
    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_MIDDLE))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_MIDDLE_PRESSED);

    // Global button released (56-58)
    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_LEFT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_LEFT_RELEASED);
    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_RIGHT))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_RIGHT_RELEASED);
    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_MIDDLE))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_GLOB_MIDDLE_RELEASED);

    // Wheel events (60-61) - global
    if (RunnerMouse_getWheelUp(mouse))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_WHEEL_UP);
    if (RunnerMouse_getWheelDown(mouse))
        Runner_executeEventForAll(runner, EVENT_MOUSE, MOUSE_WHEEL_DOWN);

    if (runner->pendingRoom >= 0) return;

    // ---[ Local events: fire only on instances whose mask the mouse is over ]---
    // Pre-lookup slots for all local subtypes to bail early if none are registered.
    int32_t slotLeftBtn      = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_LEFT_BUTTON);
    int32_t slotRightBtn     = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_RIGHT_BUTTON);
    int32_t slotMiddleBtn    = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_MIDDLE_BUTTON);
    int32_t slotNoBtn        = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_NO_BUTTON);
    int32_t slotLeftPress    = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_LEFT_PRESSED);
    int32_t slotRightPress   = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_RIGHT_PRESSED);
    int32_t slotMiddlePress  = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_MIDDLE_PRESSED);
    int32_t slotLeftRel      = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_LEFT_RELEASED);
    int32_t slotRightRel     = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_RIGHT_RELEASED);
    int32_t slotMiddleRel    = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_MIDDLE_RELEASED);
    int32_t slotEnter        = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_ENTER);
    int32_t slotLeave        = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_MOUSE, MOUSE_LEAVE);

    bool anyLocalSlot = slotLeftBtn >= 0 || slotRightBtn >= 0 || slotMiddleBtn >= 0 ||
                        slotNoBtn >= 0 || slotLeftPress >= 0 || slotRightPress >= 0 ||
                        slotMiddlePress >= 0 || slotLeftRel >= 0 || slotRightRel >= 0 ||
                        slotMiddleRel >= 0 || slotEnter >= 0 || slotLeave >= 0;
    if (!anyLocalSlot) return;

    // Snapshot all instances (local mouse events can run user code that changes the instance list)
    int32_t instCount = (int32_t) arrlen(runner->instances);
    if (instCount == 0) return;

    int32_t snapshotBase = (int32_t) arrlen(runner->instanceSnapshots);
    arrsetlen(runner->instanceSnapshots, snapshotBase + instCount);
    memcpy(&runner->instanceSnapshots[snapshotBase], runner->instances, (size_t) instCount * sizeof(Instance*));

    // Per-instance mouse-over flags
    bool* isOver = (bool*) safeMalloc((size_t) instCount * sizeof(bool));

    // Compute whether the mouse is currently over each instance's mask.
    // Enter / Leave edge detection also updates inst->mouseOver here.
    ResolvedEventTable* table = &runner->eventTable;
    repeat(instCount, i) {
        Instance* inst = runner->instanceSnapshots[snapshotBase + i];
        if (!inst->active) { isOver[i] = false; continue; }

        Sprite* spr = Collision_getSprite(dataWin, inst);
        bool over = Collision_pointInInstance(spr, inst, mx, my);
        isOver[i] = over;

        // Enter / Leave (one-shot transitions)
        bool wasOver = inst->mouseOver;
        inst->mouseOver = over;

        if (over && !wasOver && slotEnter >= 0) {
            int32_t ownerObjectIndex = -1;
            int32_t codeId = ResolvedEventTable_lookup(table, inst->objectIndex, slotEnter, &ownerObjectIndex);
            if (codeId >= 0) {
                Runner_executeResolvedEvent(runner, inst, EVENT_MOUSE, MOUSE_ENTER, codeId, ownerObjectIndex);
                if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }
            }
        } else if (!over && wasOver && slotLeave >= 0) {
            int32_t ownerObjectIndex = -1;
            int32_t codeId = ResolvedEventTable_lookup(table, inst->objectIndex, slotLeave, &ownerObjectIndex);
            if (codeId >= 0) {
                Runner_executeResolvedEvent(runner, inst, EVENT_MOUSE, MOUSE_LEAVE, codeId, ownerObjectIndex);
                if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }
            }
        }
    }

    // Button-held local events (0-2)
    if (RunnerMouse_checkButton(mouse, GML_MB_LEFT))
        fireLocalMouseSubtype(runner, MOUSE_LEFT_BUTTON, slotLeftBtn, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButton(mouse, GML_MB_RIGHT))
        fireLocalMouseSubtype(runner, MOUSE_RIGHT_BUTTON, slotRightBtn, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButton(mouse, GML_MB_MIDDLE))
        fireLocalMouseSubtype(runner, MOUSE_MIDDLE_BUTTON, slotMiddleBtn, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    // No-button local event (3): mouse over but nothing held
    if (!RunnerMouse_checkButton(mouse, GML_MB_ANY))
        fireLocalMouseSubtype(runner, MOUSE_NO_BUTTON, slotNoBtn, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    // Button-pressed local events (4-6)
    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_LEFT))
        fireLocalMouseSubtype(runner, MOUSE_LEFT_PRESSED, slotLeftPress, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_RIGHT))
        fireLocalMouseSubtype(runner, MOUSE_RIGHT_PRESSED, slotRightPress, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButtonPressed(mouse, GML_MB_MIDDLE))
        fireLocalMouseSubtype(runner, MOUSE_MIDDLE_PRESSED, slotMiddlePress, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    // Button-released local events (7-9)
    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_LEFT))
        fireLocalMouseSubtype(runner, MOUSE_LEFT_RELEASED, slotLeftRel, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_RIGHT))
        fireLocalMouseSubtype(runner, MOUSE_RIGHT_RELEASED, slotRightRel, &runner->instanceSnapshots[snapshotBase], instCount, isOver);
    if (runner->pendingRoom >= 0) { arrsetlen(runner->instanceSnapshots, snapshotBase); free(isOver); return; }

    if (RunnerMouse_checkButtonReleased(mouse, GML_MB_MIDDLE))
        fireLocalMouseSubtype(runner, MOUSE_MIDDLE_RELEASED, slotMiddleRel, &runner->instanceSnapshots[snapshotBase], instCount, isOver);

    arrsetlen(runner->instanceSnapshots, snapshotBase);
    free(isOver);
}

static int sortInstancesByObjectIndexThenInstanceIdAscending(const void* element1, const void* element2) {
    Instance* instance1 = *(Instance**) element1;
    Instance* instance2 = *(Instance**) element2;

    if (instance1->objectIndex != instance2->objectIndex) return instance1->objectIndex > instance2->objectIndex ? 1 : -1;
    if (instance1->instanceId != instance2->instanceId) return instance1->instanceId > instance2->instanceId ? 1 : -1;
    return 0;
}

static void dispatchCollisionEvents(Runner* runner) {
    DataWin* dataWin = runner->dataWin;
    // Iterate only the objects that have any collision event in their parent chain.
    int32_t* selfObjects = (runner->objectsWithAnyEventOfType != nullptr) ? runner->objectsWithAnyEventOfType[EVENT_COLLISION] : nullptr;
    if (selfObjects == nullptr) return;
    int32_t selfObjCount = (int32_t) arrlen(selfObjects);

    repeat(selfObjCount, soIdx) {
        int32_t selfObjIdx = selfObjects[soIdx];
        Instance** selfBucket = runner->instancesByExactObject[selfObjIdx];
        int32_t selfBucketCount = (int32_t) arrlen(selfBucket);
        if (selfBucketCount == 0) continue;

        // Snapshot the self bucket: collision handlers can spawn/destroy/instance_change. Iterating a snapshot also keeps newly-created instances from firing collisions in this same phase.
        int32_t selfSnapBase = (int32_t) arrlen(runner->instanceSnapshots);
        arrsetlen(runner->instanceSnapshots, selfSnapBase + selfBucketCount);
        memcpy(&runner->instanceSnapshots[selfSnapBase], selfBucket, (size_t) selfBucketCount * sizeof(Instance*));

        repeat(selfBucketCount, si) {
            Instance* self = runner->instanceSnapshots[selfSnapBase + si];
            if (!self->active) continue;

            InstanceBBox bboxSelf;
            Sprite* sprSelf;
            bool selfDirty = true;

            FlattenedCollisionEventList* eventList = &runner->flattenedCollisionEvents[self->objectIndex];
            repeat(eventList->eventCount, evtIdx) {
                FlattenedCollisionEvent* evt = &eventList->events[evtIdx];
                int32_t targetObjIndex = (int32_t) evt->targetObjectIndex;

                if (0 > evt->codeId)
                    continue;

                // Iterate only the descendant-inclusive list for the target object via a snapshot, so nested user code (collision handlers calling instance_exists, with (...), etc.) can push/pop their own snapshots above ours without corrupting this iteration.
                int32_t snapBase = Runner_pushInstancesOfObject(runner, targetObjIndex);
                int32_t snapEnd = (int32_t) arrlen(runner->instanceSnapshots);

                // The YoYo Runner sorts it by the object index THEN by the instanceId
                qsort(runner->instanceSnapshots + snapBase, snapEnd - snapBase, sizeof(Instance*), sortInstancesByObjectIndexThenInstanceIdAscending);

                for (int32_t snapIdx = snapBase; snapEnd > snapIdx; snapIdx++) {
                    Instance* other = runner->instanceSnapshots[snapIdx];
                    if (!other->active) continue;
                    if (other == self) continue;

                    // Compute bboxes
                    if (selfDirty) {
                        bboxSelf = Collision_computeBBox(runner, self);
                        sprSelf = Collision_getSprite(dataWin, self);
                        selfDirty = false;
                    }
                    InstanceBBox bboxOther = Collision_computeBBox(runner, other);

#ifdef ENABLE_VM_TRACING
                    bool traceThisPair = shouldTraceCollisionPair(runner->vmContext, dataWin, self, other);
                    if (traceThisPair && (!bboxSelf.valid || !bboxOther.valid)) {
                        fprintf(stderr, "Collision: [%s id=%d] vs [%s id=%d] bbox-invalid (selfValid=%d otherValid=%d)\n",
                            dataWin->objt.objects[self->objectIndex].name, self->instanceId,
                            dataWin->objt.objects[other->objectIndex].name, other->instanceId,
                            bboxSelf.valid, bboxOther.valid);
                    }
#endif
                    if (!bboxSelf.valid || !bboxOther.valid) continue;

                    // AABB overlap test
                    bool aabbMiss = bboxSelf.left >= bboxOther.right || bboxOther.left >= bboxSelf.right || bboxSelf.top >= bboxOther.bottom || bboxOther.top >= bboxSelf.bottom;
#ifdef ENABLE_VM_TRACING
                    if (traceThisPair) {
                        fprintf(stderr, "Collision: [%s id=%d pos=(%g,%g)] vs [%s id=%d pos=(%g,%g)] selfBB=(%g,%g,%g,%g %gx%g) otherBB=(%g,%g,%g,%g %gx%g) selfSolid=%d otherSolid=%d AABB=%s\n",
                            dataWin->objt.objects[self->objectIndex].name, self->instanceId, self->x, self->y,
                            dataWin->objt.objects[other->objectIndex].name, other->instanceId, other->x, other->y,
                            bboxSelf.left, bboxSelf.top, bboxSelf.right, bboxSelf.bottom, bboxSelf.right - bboxSelf.left, bboxSelf.bottom - bboxSelf.top,
                            bboxOther.left, bboxOther.top, bboxOther.right, bboxOther.bottom, bboxOther.right - bboxOther.left, bboxOther.bottom - bboxOther.top,
                            self->solid, other->solid, aabbMiss ? "miss" : "overlap");
                    }
#endif
                    if (aabbMiss) continue;

                    // Precise collision check if either sprite needs it (per-pixel for sepMasks==1, OBB SAT for rotated sepMasks==2).
                    Sprite* sprOther = Collision_getSprite(dataWin, other);
                    bool needsPrecise = (sprSelf != nullptr && sprSelf->sepMasks == 1) || (sprOther != nullptr && sprOther->sepMasks == 1) || Collision_obbNeedsSAT(sprSelf, self) || Collision_obbNeedsSAT(sprOther, other);

                    if (needsPrecise) {
                        bool preciseHit = Collision_instancesOverlapPrecise(runner, self, other, bboxSelf, bboxOther);
#ifdef ENABLE_VM_TRACING
                        if (traceThisPair) fprintf(stderr, "  precise=%s (selfSepMasks=%d otherSepMasks=%d)\n", preciseHit ? "hit" : "miss", sprSelf ? (int32_t)sprSelf->sepMasks : -1, sprOther ? (int32_t)sprOther->sepMasks : -1);
#endif
                        if (!preciseHit) continue;
                    }

                    // Collision detected! If either instance is solid, restore both to xprevious/yprevious.
                    bool hadSolid = self->solid || other->solid;
                    if (hadSolid) {
#ifdef ENABLE_VM_TRACING
                        if (traceThisPair) fprintf(stderr, "  solid-restore: self.solid=%d other.solid=%d self=(%g,%g)->(%g,%g) other=(%g,%g)->(%g,%g)\n", self->solid, other->solid, self->x, self->y, self->xprevious, self->yprevious, other->x, other->y, other->xprevious, other->yprevious);
#endif
                        self->x = self->xprevious;
                        self->y = self->yprevious;
                        if (self->pathIndex >= 0) self->pathPosition = self->pathPositionPrevious;
                        other->x = other->xprevious;
                        other->y = other->yprevious;
                        if (other->pathIndex >= 0) other->pathPosition = other->pathPositionPrevious;
                        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, self);
                        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, other);
                    }

                    // We don't need to call "SpatialGrid_markInstanceAsDirty" here because *technically* just because a collision happened, doesn't mean that the instances have moved
                    // And if it DOES move via GML, the variable write handlers will set it to dirty

#ifdef ENABLE_VM_TRACING
                    if (traceThisPair) fprintf(stderr, "  fire self->other: subtype=%d (%s) owner=%d (%s) codeId=%d codeName=%s\n", targetObjIndex, dataWin->objt.objects[targetObjIndex].name, evt->ownerObjectIndex, dataWin->objt.objects[evt->ownerObjectIndex].name, evt->codeId, dataWin->code.entries[evt->codeId].name);
#endif
                    executeCollisionEvent(runner, self, other, targetObjIndex, evt->codeId, evt->ownerObjectIndex);

                    // When both objects are colliding, we'll execute the SELF collision (which we already did) and THEN execute the OTHER collision too
                    // Because if we don't, the OTHER collision may never happen again because
                    // * GML code may have pushed it away
                    // * Solid collision resolution may have also pushed it away
                    // This ONLY happens if one of them was solid
                    if (hadSolid && other->active && self->active) {
                        FlattenedCollisionEvent* reverseEvt = findSymmetricCollisionEvent(runner, other, self);
#ifdef ENABLE_VM_TRACING
                        if (traceThisPair) {
                            if (reverseEvt != nullptr) fprintf(stderr, "  fire other->self: subtype=%u (%s) owner=%d (%s) codeId=%d codeName=%s  [symmetric]\n", reverseEvt->targetObjectIndex, dataWin->objt.objects[reverseEvt->targetObjectIndex].name, reverseEvt->ownerObjectIndex, dataWin->objt.objects[reverseEvt->ownerObjectIndex].name, reverseEvt->codeId, dataWin->code.entries[evt->codeId].name);
                            else fprintf(stderr, "  fire other->self: none (no matching handler)  [symmetric]\n");
                        }
#endif
                        if (reverseEvt != nullptr)
                            executeCollisionEvent(runner, other, self, (int32_t) reverseEvt->targetObjectIndex, reverseEvt->codeId, reverseEvt->ownerObjectIndex);
                    }

                    // Native parity for solids: collision event can alter path state, so run one
                    // post-event path adaptation and apply its hspeed/vspeed step.
                    if (hadSolid && self->active && other->active) {
                        adaptPath(runner, self);
                        adaptPath(runner, other);
                        if (self->hspeed != 0.0f || self->vspeed != 0.0f) {
                            self->x += self->hspeed;
                            self->y += self->vspeed;
                            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, self);
                        }
                        if (other->hspeed != 0.0f || other->vspeed != 0.0f) {
                            other->x += other->hspeed;
                            other->y += other->vspeed;
                            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, other);
                        }

                        // When we are in collision compatibility mode, we need to recheck if the player is STILL colliding after we have moved them
                        // If they are, we revert the collision
                        if (runner->collisionCompatibilityMode) {
                            InstanceBBox bboxSelf2 = Collision_computeBBox(runner, self);
                            InstanceBBox bboxOther2 = Collision_computeBBox(runner, other);
                            if (bboxSelf2.valid && bboxOther2.valid) {
                                bool aabbMiss2 = bboxSelf2.left >= bboxOther2.right || bboxOther2.left >= bboxSelf2.right || bboxSelf2.top >= bboxOther2.bottom || bboxOther2.top >= bboxSelf2.bottom;
                                bool stillColliding = false;
                                if (!aabbMiss2) {
                                    Sprite* sprSelf2 = Collision_getSprite(dataWin, self);
                                    Sprite* sprOther2 = Collision_getSprite(dataWin, other);
                                    bool needsPrecise2 = (sprSelf2 != nullptr && sprSelf2->sepMasks == 1) || (sprOther2 != nullptr && sprOther2->sepMasks == 1) || Collision_obbNeedsSAT(sprSelf2, self) || Collision_obbNeedsSAT(sprOther2, other);
                                    if (needsPrecise2) {
                                        stillColliding = Collision_instancesOverlapPrecise(runner, self, other, bboxSelf2, bboxOther2);
                                    } else {
                                        stillColliding = true;
                                    }
                                }
                                if (stillColliding) {
    #ifdef ENABLE_VM_TRACING
                                    if (traceThisPair) fprintf(stderr, "  post-event re-revert: still colliding, restoring self=(%g,%g)->(%g,%g) other=(%g,%g)->(%g,%g)\n", self->x, self->y, self->xprevious, self->yprevious, other->x, other->y, other->xprevious, other->yprevious);
    #endif
                                    self->x = self->xprevious;
                                    self->y = self->yprevious;
                                    if (self->pathIndex >= 0) self->pathPosition = self->pathPositionPrevious;
                                    other->x = other->xprevious;
                                    other->y = other->yprevious;
                                    if (other->pathIndex >= 0) other->pathPosition = other->pathPositionPrevious;
                                    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, self);
                                    SpatialGrid_markInstanceAsDirty(runner->spatialGrid, other);
                                }
                            }
                        }
                    }

                    // The collision event may have moved our instance, so we'll need to regenerate our self attributes!
                    selfDirty = true;
                }
                Runner_popInstanceSnapshot(runner, snapBase);
            }
        }

        arrsetlen(runner->instanceSnapshots, selfSnapBase);
    }
}

// ===[ View Following + Clamping ]===
// Single-axis follow with border-based scrolling, room clamping, and speed limit.
static int32_t followAxis(int32_t viewPos, int32_t viewSize, int32_t targetPos, uint32_t border, int32_t speed, int32_t roomSize) {
    int32_t pos = viewPos;

    // Border-based scrolling
    if (2 * (int32_t) border >= viewSize) {
        pos = targetPos - viewSize / 2;
    } else if (targetPos - (int32_t) border < viewPos) {
        pos = targetPos - (int32_t) border;
    } else if (targetPos + (int32_t) border > viewPos + viewSize) {
        pos = targetPos + (int32_t) border - viewSize;
    }

    // Clamp to room bounds
    if (0 > pos) pos = 0;
    if (pos + viewSize > roomSize) pos = roomSize - viewSize;

    // Speed limit
    if (speed >= 0) {
        if (pos < viewPos && viewPos - pos > speed) pos = viewPos - speed;
        if (pos > viewPos && pos - viewPos > speed) pos = viewPos + speed;
    }

    return pos;
}

static void updateViews(Runner* runner) {
    if (!runner->viewsEnabled) return;
    Room* room = runner->currentRoom;

    repeat(MAX_VIEWS, vi) {
        RuntimeView* view = &runner->views[vi];
        if (!view->enabled) continue;
        GMLCamera* camera = Runner_getCameraForView(runner, (int32_t) vi);
        if (camera == nullptr || 0 > camera->objectId) continue;

        // Find the target view instance
        Instance* target = nullptr;
        int32_t targetId = camera->objectId;

        if (targetId >= INSTANCE_ID_BASE) {
            // It's an instance ID - look it up directly
            target = hmget(runner->instancesById, targetId);
            if (target != nullptr && (!target->active || target->destroyed)) {
                target = nullptr;
            }
        } else if (targetId >= 0 && runner->dataWin->objt.count > (uint32_t) targetId) {
            // It's an object index - find first active instance of that object
            Instance** bucket = runner->instancesByObject[targetId];
            int32_t bucketCount = (int32_t) arrlen(bucket);
            repeat(bucketCount, i) {
                if (bucket[i]->active && !bucket[i]->destroyed) {
                    target = bucket[i];
                    break;
                }
            }
        }

        if (target != nullptr) {
            int32_t ix = (int32_t) GMLReal_floor(target->x);
            int32_t iy = (int32_t) GMLReal_floor(target->y);
            camera->viewX = followAxis(camera->viewX, camera->viewWidth, ix, camera->borderX, camera->speedX, (int32_t) room->width);
            camera->viewY = followAxis(camera->viewY, camera->viewHeight, iy, camera->borderY, camera->speedY, (int32_t) room->height);
            Runner_updateCameraViewSimple(camera);
        }
    }
}

static void dispatchOutsideRoomEvents(Runner* runner) {
    int32_t outsideSlot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_OTHER, OTHER_OUTSIDE_ROOM);
    if (0 > outsideSlot) return;
    ResolvedEventTable* table = &runner->eventTable;
    uint32_t entryCount;
    SlotResponderEntry* entries = ResolvedEventTable_slotEntries(table, outsideSlot, &entryCount);
    if (entryCount == 0) return;

    int32_t roomWidth = (int32_t) runner->currentRoom->width;
    int32_t roomHeight = (int32_t) runner->currentRoom->height;

    repeat(entryCount, s) {
        int32_t objIdx = entries[s].concreteObjectId;
        Instance** bucket = runner->instancesByExactObject[objIdx];
        int32_t bucketCount = (int32_t) arrlen(bucket);
        if (bucketCount == 0) continue;

        // All instances in the bucket share the same exact objectIndex, so the handler resolves to one (codeId, owner).
        int32_t ownerObjectIndex = -1;
        int32_t codeId = ResolvedEventTable_lookup(table, objIdx, outsideSlot, &ownerObjectIndex);
        if (0 > codeId) continue;

        // Snapshot the bucket: an Outside Room handler can spawn/destroy/instance_change.
        int32_t snapshotBase = (int32_t) arrlen(runner->instanceSnapshots);
        arrsetlen(runner->instanceSnapshots, snapshotBase + bucketCount);
        memcpy(&runner->instanceSnapshots[snapshotBase], bucket, (size_t) bucketCount * sizeof(Instance*));

        repeat(bucketCount, i) {
            Instance* inst = runner->instanceSnapshots[snapshotBase + i];
            if (!inst->active) continue;

            bool outside;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            if (bbox.valid) {
                outside = (0 > bbox.right || bbox.left > roomWidth || 0 > bbox.bottom || bbox.top > roomHeight);
            } else {
                outside = (0 > inst->x || inst->x > roomWidth || 0 > inst->y || inst->y > roomHeight);
            }

            if (outside && !inst->outsideRoom) {
                Runner_executeResolvedEvent(runner, inst, EVENT_OTHER, OTHER_OUTSIDE_ROOM, codeId, ownerObjectIndex);
                if (runner->pendingRoom >= 0) {
                    arrsetlen(runner->instanceSnapshots, snapshotBase);
                    return;
                }
            }

            inst->outsideRoom = outside;
        }

        arrsetlen(runner->instanceSnapshots, snapshotBase);
    }
}

static void dispatchOutsideViewEvents(Runner* runner, int32_t viewIndex) {
    int32_t subtype = OTHER_OUTSIDE_VIEW0 + viewIndex; // All subtypes are sequential so we can be quirky with it :3
    int32_t outsideSlot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_OTHER, subtype);
    if (0 > outsideSlot) return;
    ResolvedEventTable* table = &runner->eventTable;
    uint32_t entryCount;
    SlotResponderEntry* entries = ResolvedEventTable_slotEntries(table, outsideSlot, &entryCount);
    if (entryCount == 0) return;

    GMLCamera* camera = Runner_getCameraForView(runner, viewIndex);
    int32_t viewLeft = camera->viewX;
    int32_t viewTop = camera->viewY;
    int32_t viewWidth = camera->viewWidth;
    int32_t viewHeight = camera->viewHeight;
    int32_t viewRight = viewLeft + viewWidth;
    int32_t viewBottom = viewTop + viewHeight;

    repeat(entryCount, s) {
        int32_t objIdx = entries[s].concreteObjectId;
        Instance** bucket = runner->instancesByExactObject[objIdx];
        int32_t bucketCount = (int32_t) arrlen(bucket);
        if (bucketCount == 0) continue;

        // All instances in the bucket share the same exact objectIndex, so the handler resolves to one (codeId, owner).
        int32_t ownerObjectIndex = -1;
        int32_t codeId = ResolvedEventTable_lookup(table, objIdx, outsideSlot, &ownerObjectIndex);
        if (0 > codeId) continue;

        // Snapshot the bucket: an Outside Room handler can spawn/destroy/instance_change.
        int32_t snapshotBase = (int32_t) arrlen(runner->instanceSnapshots);
        arrsetlen(runner->instanceSnapshots, snapshotBase + bucketCount);
        memcpy(&runner->instanceSnapshots[snapshotBase], bucket, (size_t) bucketCount * sizeof(Instance*));

        repeat(bucketCount, i) {
            Instance* inst = runner->instanceSnapshots[snapshotBase + i];
            if (!inst->active) continue;

            bool outside;
            InstanceBBox bbox = Collision_computeBBox(runner, inst);

            if (bbox.valid) {
                outside = (viewLeft > bbox.right || bbox.left > viewRight || 0 > bbox.bottom || bbox.top > viewBottom);
            } else {
                outside = ((float) viewLeft > inst->x || inst->x > (float) viewRight || (float) viewTop > inst->y || inst->y > (float) viewBottom);
            }

            if (outside && !inst->outsideRoom) {
                Runner_executeResolvedEvent(runner, inst, EVENT_OTHER, OTHER_OUTSIDE_ROOM, codeId, ownerObjectIndex);
                if (runner->pendingRoom >= 0) {
                    arrsetlen(runner->instanceSnapshots, snapshotBase);
                    return;
                }
            }

            inst->outsideRoom = outside;
        }

        arrsetlen(runner->instanceSnapshots, snapshotBase);
    }
}

static void persistRoomState(Runner* runner, int32_t roomIndex) {
    SavedRoomState* state = &runner->savedRoomStates[roomIndex];

    // Free any previously saved instances (from an earlier visit)
    int32_t prevSavedCount = (int32_t) arrlen(state->instances);
    repeat(prevSavedCount, i) {
        hmdel(runner->instancesById, state->instances[i]->instanceId);
        Instance_free(state->instances[i]);
    }
    arrfree(state->instances);
    state->instances = nullptr;
    hmfree(state->tileLayerMap);
    state->tileLayerMap = nullptr;
    freeRuntimeLayersArray(&state->runtimeLayers);

    // Separate persistent instances (travel with player) from room instances (saved)
    Instance** keptInstances = nullptr;
    int32_t count = (int32_t) arrlen(runner->instances);
    repeat(count, i) {
        Instance* inst = runner->instances[i];
        if (inst->persistent) {
            arrput(keptInstances, inst);
        } else if (inst->active) {
            arrput(state->instances, inst);
        } else {
            hmdel(runner->instancesById, inst->instanceId);
            Instance_free(inst);
        }
    }
    arrfree(runner->instances);
    runner->instances = keptInstances;

    // The per-object lists referenced the full pre-transition instance set (persistents, saved-to-state, and soon-to-be-freed). Only the kept persistents remain live, so rebuild from scratch from the final runner->instances.
    Runner_clearAllObjectLists(runner);
    repeat((int32_t) arrlen(runner->instances), i) {
        Runner_addInstanceToObjectLists(runner, runner->instances[i]);
    }

    // Save room visual state
    memcpy(state->backgrounds, runner->backgrounds, sizeof(runner->backgrounds));
    memcpy(state->views, runner->views, sizeof(runner->views));
    state->viewsEnabled = runner->viewsEnabled;
    // Snapshot the room-scoped default cameras (whole array); user cameras are global and not snapshotted.
    memcpy(state->defaultCameras, runner->defaultCameras, sizeof(state->defaultCameras));
    state->backgroundColor = runner->backgroundColor;
    state->drawBackgroundColor = runner->drawBackgroundColor;

    // Transfer tile layer map ownership to saved state
    state->tileLayerMap = runner->tileLayerMap;
    runner->tileLayerMap = nullptr;

    // Transfer runtime layer ownership to saved state
    state->runtimeLayers = runner->runtimeLayers;
    runner->runtimeLayers = nullptr;

    state->initialized = true;
}

void Runner_handlePendingRoomChange(Runner* runner) {
    runner->profRoomParseUs = 0;
    runner->profRoomCreateUs = 0;
    runner->profRoomEndEventsUs = 0;
    runner->profRoomStartEventsUs = 0;
    runner->profRoomCleanupUs = 0;
    runner->profRoomTotalUs = 0;
    // Handle game restart
    if (runner->pendingRoom == ROOM_RESTARTGAME) {
#ifdef PLATFORM_VITA
        VitaRoomPrefetch_cancel();
#endif
        // See you soon!
        // Free the currently-loaded non-eager room before reset so lazyLoadRooms stays steady-state.
        if (runner->dataWin->lazyLoadRooms && runner->currentRoom != nullptr && !runner->currentRoom->eagerlyLoaded) {
            DataWin_freeRoomPayload(runner->currentRoom);
        }
#ifdef PLATFORM_VITA
        if (vitaCh1PreviousRoomPayload >= 0 &&
            (uint32_t)vitaCh1PreviousRoomPayload < runner->dataWin->room.count) {
            Room* cachedRoom = &runner->dataWin->room.rooms[vitaCh1PreviousRoomPayload];
            if (cachedRoom != runner->currentRoom && !cachedRoom->eagerlyLoaded && cachedRoom->payloadLoaded)
                DataWin_freeRoomPayload(cachedRoom);
        }
        vitaCh1PreviousRoomPayload = -1;
#endif
        Runner_reset(runner);
        Runner_initFirstRoom(runner);
        return;
    }

    // Handle room transition
    if (runner->pendingRoom >= 0) {
#ifdef PLATFORM_VITA
        if ((uint32_t)runner->pendingRoom < runner->dataWin->room.count &&
            !runner->dataWin->room.rooms[runner->pendingRoom].payloadLoaded &&
            !VitaRoomPrefetch_prepare(runner->pendingRoom)) {
            runner->vitaRoomTransitionWaiting = true;
            return;
        }
        runner->vitaRoomTransitionWaiting = false;
#endif
        uint64_t roomChangeBegin = nowNanos();
        int32_t oldRoomIndex = runner->currentRoomIndex;
        Room* oldRoom = runner->currentRoom;
        const char* oldRoomName = oldRoom->name;

        // Clear pendingRoom BEFORE firing Room End so the dispatch gate lets the events through.
        int32_t newRoomIndex = runner->pendingRoom;
        runner->pendingRoom = -1;

#ifdef PLATFORM_VITA
        extern void VitaBorders_prepareRoomChange(const char* nextRoomName);
        extern void VitaProbe_logLine(const char* text);
        // Collision-driven room changes are completed inside Runner_step(),
        // before playable_main can observe pendingRoom. Hold only streams
        // created by this transition at the actual transition boundary. A
        // save/menu transition may already own a longer hold that lasts until
        // its first presented frame; do not release that one here.
        AlAudioSystem* vitaAudio = (AlAudioSystem*)runner->audioSystem;
        bool vitaRoomAudioHoldOwned = oldRoomIndex > 1 && vitaAudio != nullptr;
        if (vitaRoomAudioHoldOwned) {
            // Match the stable v0.71-70 transition behaviour: mark the hold
            // without pausing the outgoing stream. New music created while
            // the destination is constructed inherits pausedByTransition,
            // while a BGM shared by both rooms keeps its playback position.
            vitaAudio->transitionHold = true;
            runner->vitaAudioTransitionWaitFrames = 2;
            VitaProbe_logLine("ROOM_AUDIO=runner_new_streams_held_during_construction");
        }
        char vitaRoomPhase[192];
        snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                 "VITA_ROOM_PHASE=begin from=%d to=%d resident=%llu",
                 oldRoomIndex, newRoomIndex,
                 (unsigned long long)(g_vitaModernGlActive ? 0ULL :
                     ((GLLegacyRenderer*)runner->renderer)->residentTextureBytes));
        VitaProbe_logLine(vitaRoomPhase);
        const char* vitaNextRoomName =
            (uint32_t)newRoomIndex < runner->dataWin->room.count ?
            runner->dataWin->room.rooms[newRoomIndex].name : NULL;
        VitaBorders_prepareRoomChange(vitaNextRoomName);
        snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                 "VITA_ROOM_PHASE=border_release_complete from=%d to=%d",
                 oldRoomIndex, newRoomIndex);
        VitaProbe_logLine(vitaRoomPhase);
#endif

        // Fire Room End for all instances
        uint64_t phaseBegin = nowNanos();
        Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ROOM_END);
        runner->profRoomEndEventsUs = (nowNanos() - phaseBegin) / 1000ULL;
#ifdef PLATFORM_VITA
        snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                 "VITA_ROOM_PHASE=room_end_complete from=%d to=%d us=%llu",
                 oldRoomIndex, newRoomIndex,
                 (unsigned long long)runner->profRoomEndEventsUs);
        VitaProbe_logLine(vitaRoomPhase);
#endif
        require(runner->dataWin->room.count > (uint32_t) newRoomIndex);
        const char* newRoomName = runner->dataWin->room.rooms[newRoomIndex].name;

        fprintf(stderr, "Room changed: %s (room %d) -> %s (room %d)\n", oldRoomName, oldRoomIndex, newRoomName, newRoomIndex);

        // If the old room is persistent, save its instance and visual state
        if (oldRoom->persistent) {
            persistRoomState(runner, oldRoomIndex);
        }

        // Free the outgoing room's payload under lazyLoadRooms, unless it's eagerly pinned or we're restarting the same room (initRoom would just re-load it).
        if (runner->dataWin->lazyLoadRooms && !oldRoom->eagerlyLoaded && newRoomIndex != oldRoomIndex) {
#ifdef PLATFORM_VITA
            if (g_vitaActiveChapter == 1) {
                // Retain only the immediately previous Chapter 1 room. When
                // backtracking, the destination is already parsed and the
                // room we just left becomes the new single cached payload.
                if (vitaCh1PreviousRoomPayload >= 0 &&
                    vitaCh1PreviousRoomPayload != oldRoomIndex &&
                    vitaCh1PreviousRoomPayload != newRoomIndex &&
                    (uint32_t)vitaCh1PreviousRoomPayload < runner->dataWin->room.count) {
                    Room* staleRoom = &runner->dataWin->room.rooms[vitaCh1PreviousRoomPayload];
                    if (!staleRoom->eagerlyLoaded && staleRoom->payloadLoaded)
                        DataWin_freeRoomPayload(staleRoom);
                }
                vitaCh1PreviousRoomPayload = oldRoomIndex;
            } else {
                DataWin_freeRoomPayload(oldRoom);
                vitaCh1PreviousRoomPayload = -1;
            }
#else
            DataWin_freeRoomPayload(oldRoom);
#endif
        }

        // Parse the destination before the Vita texture trim so its ROOM data
        // can act as a conservative atlas manifest. Parsing is still measured
        // separately from runtime instance/surface construction.
        Room* newRoom = &runner->dataWin->room.rooms[newRoomIndex];
        if (!newRoom->payloadLoaded) {
            phaseBegin = nowNanos();
            DataWin_loadRoomPayload(runner->dataWin, newRoomIndex);
            runner->profRoomParseUs = (nowNanos() - phaseBegin) / 1000ULL;
        }

#ifdef PLATFORM_VITA
        if (!g_vitaModernGlActive) {
        // The Vita build currently uses the legacy renderer. Drop only stale
        // scenery from the outgoing room before the next room allocates its
        // atlases; UI/font pages remain pinned and Original quality stays full.
        // Chapter 5's town rooms share nearly their entire atlas set. Trimming
        // them to 56 MiB forced 50+ MiB to be decoded/uploaded again on every
        // doorway, producing 9-10 second transitions. Keep that shared set and
        // reserve the aggressive trim for genuinely different areas such as
        // the Light World -> Dark World transition.
        bool vitaSharedTownTransition =
            strncmp(oldRoomName, "room_town_", 10) == 0 &&
            strncmp(newRoomName, "room_town_", 10) == 0;
        bool oldDarkCastle = strncmp(oldRoomName, "room_dw_castle_", 15) == 0 ||
                             strncmp(oldRoomName, "room_dw_ralsei_castle_", 22) == 0;
        bool newDarkCastle = strncmp(newRoomName, "room_dw_castle_", 15) == 0 ||
                             strncmp(newRoomName, "room_dw_ralsei_castle_", 22) == 0;
        bool vitaSharedDarkCastleTransition = oldDarkCastle && newDarkCastle;
        // Chapter 5's Flower Castle uses a separate room prefix from the
        // ordinary Ralsei castle but shares its character, battle and scenery
        // atlases across every doorway. Treating each room_dw_fcastle_* change
        // as unrelated discarded pages 45/46/47/51/54/56 and recreated them
        // while the encounter room was already instantiating its actors.
        bool oldFlowerCastle = strncmp(oldRoomName, "room_dw_fcastle_", 16) == 0;
        bool newFlowerCastle = strncmp(newRoomName, "room_dw_fcastle_", 16) == 0;
        bool vitaSharedFlowerCastleTransition = oldFlowerCastle && newFlowerCastle;
        bool vitaFloweryBattleDestination = g_vitaActiveChapter == 5 &&
            strcmp(newRoomName, "room_dw_fcastle_flowery") == 0;
        bool vitaSharedCouchTransition =
            strncmp(oldRoomName, "room_dw_couch_", 14) == 0 &&
            strncmp(newRoomName, "room_dw_couch_", 14) == 0;
        // Chapter 3's nondescript/backstage sequence flows directly into the
        // couch-overworld slides and reuses character/effect atlases. Treating
        // every doorway as unrelated discarded the working set, producing
        // measured 0.9-3.6 second first-frame uploads in field/classroom/
        // hallway/couch_05. The video room remains excluded below.
        bool oldCh3Backstage = g_vitaActiveChapter == 3 &&
            (strncmp(oldRoomName, "room_dw_nondescript_", 22) == 0 ||
             strncmp(oldRoomName, "room_dw_couch_overworld", 23) == 0);
        bool newCh3Backstage = g_vitaActiveChapter == 3 &&
            (strncmp(newRoomName, "room_dw_nondescript_", 22) == 0 ||
             strncmp(newRoomName, "room_dw_couch_overworld", 23) == 0);
        bool vitaSharedCh3BackstageTransition = oldCh3Backstage && newCh3Backstage;
        // The native decoder requires contiguous memory. Do not carry the
        // previous couch-room atlas set into the otherwise black video room.
        bool vitaChapter3VideoDestination = g_vitaActiveChapter == 3 &&
            strcmp(newRoomName, "room_dw_couch_video") == 0;
        bool oldCh3TvBoard = strncmp(oldRoomName, "room_ch3_", 9) == 0 ||
                              strncmp(oldRoomName, "room_dw_teevie_", 15) == 0 ||
                              strncmp(oldRoomName, "room_dw_tv_", 11) == 0 ||
                              strncmp(oldRoomName, "room_board_", 11) == 0;
        bool newCh3TvBoard = strncmp(newRoomName, "room_ch3_", 9) == 0 ||
                              strncmp(newRoomName, "room_dw_teevie_", 15) == 0 ||
                              strncmp(newRoomName, "room_dw_tv_", 11) == 0 ||
                              strncmp(newRoomName, "room_board_", 11) == 0;
        // Gameshow, Teevie and board rooms share the 17/19/24/26-29 atlas
        // core. Treating gameshow -> board as unrelated discarded 13 pages
        // and spent about three seconds recreating the same TV/character set.
        bool vitaSharedCh3TvBoardTransition = oldCh3TvBoard && newCh3TvBoard;
        // Green Room, its Teevie preview and the B3BS interstitial are a tight
        // Chapter 3 loop and reuse most of the same character/UI/TV atlases.
        // Treating each doorway as unrelated evicted 30-40 MiB only to reload
        // it seconds later, producing measured 3.9-6.0 second transitions.
        bool oldCh3GreenCluster = g_vitaActiveChapter == 3 &&
            (strcmp(oldRoomName, "room_dw_green_room") == 0 ||
             strcmp(oldRoomName, "room_dw_teevie_preview") == 0 ||
             strcmp(oldRoomName, "room_dw_b3bs_interstitial") == 0);
        bool newCh3GreenCluster = g_vitaActiveChapter == 3 &&
            (strcmp(newRoomName, "room_dw_green_room") == 0 ||
             strcmp(newRoomName, "room_dw_teevie_preview") == 0 ||
             strcmp(newRoomName, "room_dw_b3bs_interstitial") == 0);
        bool vitaSharedCh3GreenTransition = oldCh3GreenCluster && newCh3GreenCluster;
        bool oldCh1CastleArea = strncmp(oldRoomName, "room_cc", 7) == 0 ||
                                strncmp(oldRoomName, "room_forest", 11) == 0 ||
                                strncmp(oldRoomName, "room_field", 10) == 0 ||
                                strncmp(oldRoomName, "room_castle", 11) == 0 ||
                                strncmp(oldRoomName, "room_shop", 9) == 0 ||
                                strncmp(oldRoomName, "room_legend", 11) == 0;
        bool newCh1CastleArea = strncmp(newRoomName, "room_cc", 7) == 0 ||
                                strncmp(newRoomName, "room_forest", 11) == 0 ||
                                strncmp(newRoomName, "room_field", 10) == 0 ||
                                strncmp(newRoomName, "room_castle", 11) == 0 ||
                                strncmp(newRoomName, "room_shop", 9) == 0 ||
                                strncmp(newRoomName, "room_legend", 11) == 0;
        bool oldCh1TownArea = strncmp(oldRoomName, "room_town", 9) == 0 ||
                              strncmp(oldRoomName, "room_beach", 10) == 0;
        bool newCh1TownArea = strncmp(newRoomName, "room_town", 9) == 0 ||
                              strncmp(newRoomName, "room_beach", 10) == 0;
        bool vitaSharedCh1AreaTransition = g_vitaActiveChapter == 1 &&
            ((oldCh1CastleArea && newCh1CastleArea) ||
             (oldCh1TownArea && newCh1TownArea) ||
             (strncmp(oldRoomName, "room_dark", 9) == 0 &&
              strncmp(newRoomName, "room_dark", 9) == 0));
        // Chapter 2 is split into large atlas families. Preserve pages while
        // moving inside one family, but leave 16 MiB of headroom when crossing
        // Castle/Cyber/Mansion boundaries so the destination does not defer
        // its first visible atlas until after gameplay has resumed.
        int oldCh2Region =
            (strstr(oldRoomName, "dw_cyber") || strstr(oldRoomName, "dw_city")) ? 1 :
            (strstr(oldRoomName, "dw_mansion")) ? 2 :
            (strstr(oldRoomName, "dw_castle") || strstr(oldRoomName, "dw_ralsei")) ? 3 : 0;
        int newCh2Region =
            (strstr(newRoomName, "dw_cyber") || strstr(newRoomName, "dw_city")) ? 1 :
            (strstr(newRoomName, "dw_mansion")) ? 2 :
            (strstr(newRoomName, "dw_castle") || strstr(newRoomName, "dw_ralsei")) ? 3 : 0;
        bool vitaSharedCh2AreaTransition = g_vitaActiveChapter == 2 &&
            oldCh2Region != 0 && oldCh2Region == newCh2Region;
        // Mansion rooms occasionally build a 1920x480 surface and a dynamic
        // sprite from it during Create.  Neither allocation is part of the
        // atlas counter.  Retaining the generic 104 MiB regional cache left
        // 6.5 MiB free before east_2f_a and the temporary pair exhausted the
        // VitaGL pool.  Start internal Mansion doors at 88 MiB, then allow the
        // regional cache to settle at its separate 92 MiB runtime ceiling.
        bool vitaSharedCh2MansionTransition = vitaSharedCh2AreaTransition &&
            newCh2Region == 2;
        bool vitaCh2HeavyCyberDestination = g_vitaActiveChapter == 2 &&
            (strcmp(newRoomName, "room_dw_cyber_teacup_final") == 0 ||
             strcmp(newRoomName, "room_dw_cyber_rollercoaster") == 0 ||
             strcmp(newRoomName, "room_dw_cyber_viro_ring") == 0 ||
             strcmp(newRoomName, "room_dw_cyber_maze_queenscreen") == 0 ||
             strcmp(newRoomName, "room_dw_cyber_musical_shop") == 0 ||
             // Room Navigator can jump straight from another Cyber room into
             // a battle. Without an explicit trim this inherited the full
             // 104 MiB regional cache and pages 2/5/7/17/22 replaced each
             // other every frame, producing the measured 0-2 FPS loop.
              strstr(newRoomName, "room_dw_cyber_battle") != nullptr);
        bool vitaCh2BattleDestination = g_vitaActiveChapter == 2 &&
            strstr(newRoomName, "room_dw_cyber_battle") != nullptr;
        bool vitaCh2DynamicAssetDestination = g_vitaActiveChapter == 2 &&
            (strstr(newRoomName, "room_dw_cyber_intro_2") != nullptr ||
             strstr(newRoomName, "room_dw_cyber_intro_connector") != nullptr ||
             strstr(newRoomName, "room_dw_cyber_keyboard") != nullptr);
        bool vitaCh2HeavyCastleDestination = g_vitaActiveChapter == 2 &&
            (strcmp(newRoomName, "room_dw_castle_area_1") == 0 ||
             strcmp(newRoomName, "room_dw_castle_area_2") == 0 ||
             strcmp(newRoomName, "room_dw_castle_area_2_transformed") == 0 ||
             strcmp(newRoomName, "room_shop1") == 0);
        // Town rooms share most character, dialogue and scenery pages, but the
        // bordered cache is capped at 96 MiB.  Keeping the previous 108 MiB
        // trim target therefore performed no transition cleanup: the first
        // font/character atlas of the new room had no safe victim in the same
        // frame, disappeared, and could exhaust the 16 MiB upload staging block.
        // Retain 80 MiB of the shared set and reserve space for the incoming
        // room plus its surfaces. The previous 88 MiB target could leave no
        // contiguous graphics block for glFramebufferTexture2D.
        bool vitaSharedTransition = !vitaChapter3VideoDestination &&
                                    (vitaSharedTownTransition ||
                                    vitaSharedDarkCastleTransition ||
                                    vitaSharedFlowerCastleTransition ||
                                    vitaSharedCouchTransition ||
                                    vitaSharedCh3BackstageTransition ||
                                    vitaSharedCh3TvBoardTransition ||
                                    vitaSharedCh3GreenTransition ||
                                    vitaSharedCh1AreaTransition ||
                                    vitaSharedCh2AreaTransition);
        // Keep the shared set below the Chapter 5 upload ceiling. Retaining
        // 84 MiB here allowed the third town room to finish at 88.5 MiB of
        // tracked atlases and exhausted untracked VitaGL/surface headroom.
        // Dark Castle used a stale 92 MiB target even though Chapter 5 now
        // admits at most 88 MiB. Consequently castle_town -> castle_tv did no
        // cleanup and started surface/atlas creation at the full ceiling.
        // Retain the same 80 MiB shared core used by the Light World town.
        // Preserve the common pages, but leave enough space for at least two
        // incoming BC3 atlases plus surfaces. The previous 80 MiB shared trim
        // reached the 96 MiB VitaGL ceiling during creation and exhausted both
        // RAM/PHY fallback pools before the next doorway.
        // A 76 MiB retained set still grew to 80 MiB while room construction
        // was finishing, leaving no swap/GC reserve and aborting inside
        // vglSwapBuffers. Keep the common border, but start shared Chapter 5
        // transitions at 60 MiB. The town_south -> town_mid trace entered the
        // destination with CDRAM already at 96 MiB and stopped while uploading
        // page 39: retaining 72 MiB left room for only two BC3 pages, while the
        // destination also creates four surfaces and several transient pages.
        // The border itself remains resident when both rooms resolve to the
        // same path; only stale atlas pages are reclaimed here.
        GLLegacyRenderer* vitaLegacy = (GLLegacyRenderer*)runner->renderer;
        GLLegacyRenderer_prepareRoomTextureSet(vitaLegacy, newRoom);
        // Non-shared transitions must fully retire the previous room/menu
        // working set. Otherwise a resident set below the old target can pass
        // through untouched and collide with the destination room's pages.
        // Chapter 5 now uses reviewed 2048px compressed scenery plus explicit
        // VitaGL collection. Keep 32 MiB before its two-page static preload:
        // the previous 48 MiB target became 55 MiB after preload, leaving room
        // for only two more BC3 pages. Castle Town then cycled visible pages
        // 45/46/56/59 every few frames at roughly 0.9 seconds per upload.
        // Static destination pages remain protected by textureRoomPreload;
        // dynamically used pages naturally replace the stale outgoing set.
        // Chapter 2 commonly arrives here at its exact 104 MiB ceiling. Keep
        // the destination's static manifest, but release 16 MiB of stale room
        // pages so the first visible character/background can load without a
        // retry cycle. The room-trim routine now removes the selected stale
        // page deterministically instead of accidentally evicting another one.
        // Chapter 2's renderer ceiling is 104 MiB. Trimming it to 88 MiB at
        // every doorway immediately reloaded the same 16 MiB working set and
        // produced missing/stale atlas contents despite otherwise good FPS.
        uint64_t vitaTrimMiB = vitaChapter3VideoDestination ? 0ULL :
            g_vitaActiveChapter == 2 ?
            (vitaCh2BattleDestination ? 48ULL :
             vitaCh2DynamicAssetDestination ? 84ULL :
             vitaCh2HeavyCyberDestination ? 68ULL :
             vitaCh2HeavyCastleDestination ? 76ULL :
             vitaSharedCh2MansionTransition ? 88ULL :
             vitaSharedCh2AreaTransition ? 104ULL : 80ULL) :
            (vitaSharedTransition ?
            (g_vitaActiveChapter == 5 ?
             // Flowery creates roughly 21 MiB of render surfaces and needs
             // several battle-only atlases. Entering with the ordinary
             // Flower Castle 64 MiB shared set reached 99.5 MiB in VitaGL,
             // while another 16 MiB was still retired, corrupting/missing
             // sprites and occasionally aborting the first battle frame.
             // Keep only the destination manifest plus a 40 MiB shared core.
             (vitaFloweryBattleDestination ? 32ULL :
              vitaSharedFlowerCastleTransition ? 64ULL : 32ULL) :
             vitaSharedCh1AreaTransition ? 80ULL :
             // RGBA4444 TV pages use 8 MiB each. Start at 48 MiB so the
             // two-page preload still leaves room for four dynamically drawn
             // pages without immediately evicting the incoming room again.
             vitaSharedCh3TvBoardTransition ? 48ULL :
             vitaSharedCh3GreenTransition ? 48ULL : 36ULL) : 0ULL);
        GLLegacyRenderer_trimTextureCacheForRoomChange(
            (GLLegacyRenderer*)runner->renderer,
            vitaTrimMiB * 1024ULL * 1024ULL,
            g_vitaActiveChapter == 2 ? vitaSharedCh2MansionTransition : !vitaSharedTransition);
        GLLegacyRenderer_collectRetiredTexturesForRoomChange(
            (GLLegacyRenderer*)runner->renderer);
        uint32_t vitaPreloadedPages = GLLegacyRenderer_preloadRoomTextureSet(
            (GLLegacyRenderer*)runner->renderer);
        snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                 "VITA_ROOM_PHASE=texture_trim_complete from=%d to=%d resident=%llu preloaded=%u",
                 oldRoomIndex, newRoomIndex,
                 (unsigned long long)((GLLegacyRenderer*)runner->renderer)->residentTextureBytes,
                 vitaPreloadedPages);
        VitaProbe_logLine(vitaRoomPhase);
        } else {
            // The modern backend has its own RGBA4444 LRU and cannot consume
            // the legacy room manifest. The Chapter 3 board transition is a
            // special safety case: repeated real-Vita GPU crashes occur while
            // the gameshow working set is being evicted and the board/battle
            // pages are uploaded. Fence and clear stale pages before room
            // construction, then warm the small board core observed in logs.
            GLRenderer* vitaModern = (GLRenderer*)runner->renderer;
            bool vitaCh3Board2Destination =
                g_vitaActiveChapter == 3 && strcmp(newRoomName, "room_board_2") == 0;
            if (vitaCh3Board2Destination) {
                GLRenderer_trimTextureCacheForRoomChange(vitaModern, 0);
                static const uint16_t boardCorePages[] = {
                    2, 0, 132, 190, 210, 217, 261, 268, 293, 307,
                    311, 315, 316, 317, 335, 339, 343, 388, 389, 390,
                    392, 427
                };
                uint32_t warmed = 0;
                for (uint32_t pi = 0;
                     pi < sizeof(boardCorePages) / sizeof(boardCorePages[0]); ++pi) {
                    uint32_t page = boardCorePages[pi];
                    if (page >= runner->dataWin->txtr.count) continue;
                    if (GLRenderer_ensureTextureLoaded(vitaModern, page)) warmed++;
                    if ((pi & 3u) == 3u) glFinish();
                }
                glFinish();
                snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                         "MODERN_GL_CH3_BOARD_PREWARM room=%s pages=%u requested=%u resident=%llu",
                         newRoomName, warmed,
                         (unsigned)(sizeof(boardCorePages) / sizeof(boardCorePages[0])),
                         (unsigned long long)vitaModern->residentTextureBytes);
                VitaProbe_logLine(vitaRoomPhase);
            } else {
                uint64_t modernTrimMiB = g_vitaActiveChapter == 5 ? 8ULL : 16ULL;
                if (VitaGpuRules_modernSynchronousEviction(
                        g_vitaActiveChapter, newRoomName))
                    modernTrimMiB = 0ULL;
                GLRenderer_trimTextureCacheForRoomChange(
                    vitaModern, modernTrimMiB * 1024ULL * 1024ULL);
            }
            VitaProbe_logLine("VITA_ROOM_PHASE=modern_gl_texture_lru_managed_by_renderer");
        }
#endif

        // Keep lazy payload parsing separate from runtime room construction.
        phaseBegin = nowNanos();
        initRoom(runner, newRoomIndex);
        runner->profRoomCreateUs = (nowNanos() - phaseBegin) / 1000ULL;
#ifdef PLATFORM_VITA
        snprintf(vitaRoomPhase, sizeof(vitaRoomPhase),
                 "VITA_ROOM_PHASE=create_complete from=%d to=%d us=%llu",
                 oldRoomIndex, newRoomIndex,
                 (unsigned long long)runner->profRoomCreateUs);
        VitaProbe_logLine(vitaRoomPhase);
#endif

        // Fire Room Start for all instances
        phaseBegin = nowNanos();
        Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ROOM_START);
        runner->profRoomStartEventsUs = (nowNanos() - phaseBegin) / 1000ULL;

        phaseBegin = nowNanos();
        Runner_cleanupDestroyedInstances(runner);
        Runner_sweepDeadStructs(runner);

        runner->profRoomCleanupUs = (nowNanos() - phaseBegin) / 1000ULL;
        runner->profRoomTotalUs = (nowNanos() - roomChangeBegin) / 1000ULL;
#ifdef PLATFORM_VITA
        if (vitaRoomAudioHoldOwned) {
            // Do not release destination music at the end of Room Start. The
            // first one or two frames may still upload several atlases and
            // drain the new stream before it can be heard. Existing music was
            // never paused (transitionHold was assigned directly above), so it
            // can continue until the destination is visibly ready.
            runner->vitaAudioTransitionWaitFrames = 2;
            VitaProbe_logLine("ROOM_AUDIO=runner_new_streams_waiting_for_presented_frame");
        }
#endif
    }
}

void Runner_setObjectDisabled(Runner* runner, const char* objectName, bool disabled) {
    if (runner == nullptr || objectName == nullptr || objectName[0] == '\0') return;
    if (disabled) {
        shput(runner->disabledObjects, safeStrdup(objectName), 1);
    } else if (runner->disabledObjects != nullptr) {
        shdel(runner->disabledObjects, objectName);
    }
}

bool Runner_enableGameTrophies(Runner* runner) {
    if (runner == nullptr || runner->vmContext == nullptr ||
        runner->instancesByObject == nullptr) return false;

    int32_t objectIndex = -1;
    repeat(runner->dataWin->objt.count, i) {
        const char* name = runner->dataWin->objt.objects[i].name;
        if (name != nullptr && strcmp(name, "obj_event_manager") == 0) {
            objectIndex = (int32_t)i;
            break;
        }
    }
    if (objectIndex < 0) return false;

    int32_t enabledVar = VM_getOrAllocateVarID(runner->vmContext, "trophies_enabled");
    Instance** managers = runner->instancesByObject[objectIndex];
    bool found = false;
    repeat(arrlen(managers), i) {
        Instance* manager = managers[i];
        if (manager == nullptr || manager->destroyed) continue;
        Instance_setSelfVar(manager, enabledVar, RValue_makeBool(true));
        found = true;
    }
    return found;
}

void Runner_noteLegacyTrophy(Runner* runner, int32_t id, const char* reason) {
#ifdef PLATFORM_VITA
    if (runner == nullptr || !runner->vitaLegacyTrophyMode || id <= 0 || id >= 30) return;
    uint32_t bit = 1U << id;
    if ((runner->vitaLegacyTrophyMask & bit) != 0) return;
    runner->vitaLegacyTrophyMask |= bit;
    char detail[128];
    snprintf(detail, sizeof(detail), "TROPHY_BRIDGE=legacy id=%d reason=%s",
             id, reason != nullptr ? reason : "unknown");
    VitaProbe_logLine(detail);
#else
    (void)runner; (void)id; (void)reason;
#endif
}

void Runner_getGameTrophies(Runner* runner, bool unlocked[30]) {
    if (unlocked == nullptr) return;
    memset(unlocked, 0, sizeof(bool) * 30);
    if (runner == nullptr || runner->vmContext == nullptr ||
        runner->vmContext->globalScopeInstance == nullptr) return;

    int32_t trophiesVar = VM_getOrAllocateVarID(runner->vmContext, "trophies");
    RValue trophies = Instance_getSelfVar(runner->vmContext->globalScopeInstance, trophiesVar);
    if (trophies.type == RVALUE_ARRAY && trophies.array != nullptr) {
        repeat(GMLArray_length1D(trophies.array), i) {
            RValue* value = GMLArray_slot(trophies.array, i);
            if (value == nullptr || value->type == RVALUE_UNDEFINED) continue;
            int32_t id = RValue_toInt32(*value);
            if (id >= 0 && id < 30) unlocked[id] = true;
        }
    }

    // DELTARUNE does not move newly earned trophies into global.trophies
    // immediately.  The event manager first stores them in trophies_queue and
    // normally resolves that queue during a room-end/loading event.  Waiting
    // for that event made equipment and other mid-room trophies appear broken
    // on Vita (and they could be lost if the process ended first).  Treat the
    // pending queue as earned for the native/local bridge while preserving the
    // game's original queue lifecycle inside the VM.
    int32_t objectIndex = -1;
    repeat(runner->dataWin->objt.count, i) {
        const char* name = runner->dataWin->objt.objects[i].name;
        if (name != nullptr && strcmp(name, "obj_event_manager") == 0) {
            objectIndex = (int32_t)i;
            break;
        }
    }
    if (objectIndex >= 0 && runner->instancesByObject != nullptr) {
        int32_t queueVar = VM_getOrAllocateVarID(runner->vmContext, "trophies_queue");
        Instance** managers = runner->instancesByObject[objectIndex];
        repeat(arrlen(managers), i) {
            Instance* manager = managers[i];
            if (manager == nullptr || manager->destroyed) continue;
            RValue pending = Instance_getSelfVar(manager, queueVar);
            if (pending.type != RVALUE_ARRAY || pending.array == nullptr) continue;
            repeat(GMLArray_length1D(pending.array), j) {
                RValue* value = GMLArray_slot(pending.array, j);
                if (value == nullptr || value->type == RVALUE_UNDEFINED) continue;
                int32_t id = RValue_toInt32(*value);
                if (id >= 0 && id < 30) unlocked[id] = true;
            }
        }
    }

#ifdef PLATFORM_VITA
    if (runner->vitaLegacyTrophyMode) {
        int32_t chapterVar = VM_getOrAllocateVarID(runner->vmContext, "chapter");
        int32_t chapter = RValue_toInt32(Instance_getSelfVar(
            runner->vmContext->globalScopeInstance, chapterVar));

        // Chapter completion: use the actual credits destination rather than a
        // translated string or timing heuristic. Chapter 1 owns obj_credits;
        // Chapter 2 transitions to room_ed when its credits sequence begins.
        if (chapter == 1 && runner->vitaLegacyCreditsObjectIndex >= 0 &&
            runner->instancesByObject != nullptr) {
            Instance** credits = runner->instancesByObject[runner->vitaLegacyCreditsObjectIndex];
            repeat(arrlen(credits), i) {
                if (credits[i] != nullptr && !credits[i]->destroyed) {
                    Runner_noteLegacyTrophy(runner, 1, "chapter1_credits");
                    break;
                }
            }
        } else if (chapter == 2 && runner->currentRoom != nullptr &&
                   runner->currentRoom->name != nullptr &&
                   strcmp(runner->currentRoom->name, "room_ed") == 0) {
            Runner_noteLegacyTrophy(runner, 2, "chapter2_room_ed");
        }

        // PAIN compatibility for old translations: award on the first real HP
        // decrease in Ch1/Ch2. Establish a short stable baseline per room so a
        // save/room initialization cannot be mistaken for damage.
        int32_t hpVar = VM_getOrAllocateVarID(runner->vmContext, "hp");
        RValue hpValue = Instance_getSelfVar(runner->vmContext->globalScopeInstance, hpVar);
        if (hpValue.type == RVALUE_ARRAY && hpValue.array != nullptr) {
            int32_t currentHp[5] = {0,0,0,0,0};
            int32_t hpCount = GMLArray_length1D(hpValue.array);
            for (int32_t i = 0; i < 5 && i < hpCount; ++i) {
                RValue* slot = GMLArray_slot(hpValue.array, i);
                currentHp[i] = (slot == nullptr || slot->type == RVALUE_UNDEFINED) ? 0 : RValue_toInt32(*slot);
            }
            if (runner->vitaLegacyHpRoomIndex != runner->currentRoomIndex) {
                runner->vitaLegacyHpRoomIndex = runner->currentRoomIndex;
                memcpy(runner->vitaLegacyHp, currentHp, sizeof(currentHp));
                runner->vitaLegacyHpStableFrames = 1;
                runner->vitaLegacyHpInitialized = false;
            } else if (!runner->vitaLegacyHpInitialized) {
                if (memcmp(runner->vitaLegacyHp, currentHp, sizeof(currentHp)) != 0) {
                    memcpy(runner->vitaLegacyHp, currentHp, sizeof(currentHp));
                    runner->vitaLegacyHpStableFrames = 1;
                } else if (++runner->vitaLegacyHpStableFrames >= 4) {
                    runner->vitaLegacyHpInitialized = true;
                }
            } else {
                bool tookDamage = false;
                for (int32_t i = 1; i < 5 && i < hpCount; ++i) {
                    if (currentHp[i] < runner->vitaLegacyHp[i]) {
                        tookDamage = true;
                        break;
                    }
                }
                memcpy(runner->vitaLegacyHp, currentHp, sizeof(currentHp));
                if (tookDamage && (chapter == 1 || chapter == 2))
                    Runner_noteLegacyTrophy(runner, chapter == 1 ? 5 : 6, "first_hp_drop");
            }
        }

        // STRENGTHEN 2-A: old Chapter 2 translations still increment flag[65]
        // inside scr_levelup(). Observe that exact state change.
        if (chapter == 2) {
            int32_t flagVar = VM_getOrAllocateVarID(runner->vmContext, "flag");
            RValue flags = Instance_getSelfVar(runner->vmContext->globalScopeInstance, flagVar);
            if (flags.type == RVALUE_ARRAY && flags.array != nullptr && GMLArray_length1D(flags.array) > 65) {
                RValue* slot = GMLArray_slot(flags.array, 65);
                int32_t flag65 = (slot == nullptr || slot->type == RVALUE_UNDEFINED) ? 0 : RValue_toInt32(*slot);
                if (!runner->vitaLegacyStrengthInitialized) {
                    if (runner->vitaLegacyStrengthStableFrames == 0 || flag65 != runner->vitaLegacyStrengthFlag65) {
                        runner->vitaLegacyStrengthFlag65 = flag65;
                        runner->vitaLegacyStrengthStableFrames = 1;
                    } else if (++runner->vitaLegacyStrengthStableFrames >= 4) {
                        runner->vitaLegacyStrengthInitialized = true;
                    }
                } else {
                    if (flag65 > runner->vitaLegacyStrengthFlag65)
                        Runner_noteLegacyTrophy(runner, 19, "flag65_levelup");
                    runner->vitaLegacyStrengthFlag65 = flag65;
                }
            }
        }

        for (int id = 1; id < 30; ++id)
            if ((runner->vitaLegacyTrophyMask & (1U << id)) != 0) unlocked[id] = true;
    }

    // Equipment fallback remains sampled every 30 frames. Runner_getGameTrophies
    // now observes the transient event-manager trophy queue every frame, but
    // equipment needs a slower stable baseline across room/save initialization.
    if ((runner->frameCount % 30U) == 0U) {
        // Equipment changes can be delayed or lost by translated event-manager
        // scripts. Track weapons and armour independently so armour never gets
        // incorrectly reported as SWORD.
        // Some translated data.win builds play snd_equip but do not publish the
        // event-manager queue in the same frame. Mirror only the equipped arrays
        // (never inventory), so acquiring an item alone cannot unlock the trophy.
        static const char* weaponVars[] = { "charweapon", "lweapon" };
        static const char* armorVars[] = { "chararmor1", "chararmor2" };
        uint64_t fingerprints[2] = { 1469598103934665603ULL, 1469598103934665603ULL };
        const char** groups[2] = { weaponVars, armorVars };
        const uint32_t groupSizes[2] = { 2, 2 };
        for (uint32_t group = 0; group < 2; ++group) {
          for (uint32_t v = 0; v < groupSizes[group]; ++v) {
            int32_t varId = VM_getOrAllocateVarID(runner->vmContext, groups[group][v]);
            RValue equipped = Instance_getSelfVar(runner->vmContext->globalScopeInstance, varId);
            fingerprints[group] ^= (uint64_t)(uint32_t)equipped.type;
            fingerprints[group] *= 1099511628211ULL;
            if (equipped.type == RVALUE_ARRAY && equipped.array != nullptr) {
                int32_t count = GMLArray_length1D(equipped.array);
                fingerprints[group] ^= (uint64_t)(uint32_t)count;
                fingerprints[group] *= 1099511628211ULL;
                repeat(count, i) {
                    RValue* slot = GMLArray_slot(equipped.array, i);
                    int32_t value = (slot == nullptr || slot->type == RVALUE_UNDEFINED)
                        ? INT32_MIN : RValue_toInt32(*slot);
                    fingerprints[group] ^= (uint64_t)(uint32_t)value;
                    fingerprints[group] *= 1099511628211ULL;
                }
            } else {
                fingerprints[group] ^= (uint64_t)(uint32_t)RValue_toInt32(equipped);
                fingerprints[group] *= 1099511628211ULL;
            }
          }
        }
        if (!runner->vitaEquipmentFingerprintInitialized) {
            if (runner->vitaEquipmentFingerprintStablePolls == 0 ||
                runner->vitaWeaponFingerprint != fingerprints[0] ||
                runner->vitaArmorFingerprint != fingerprints[1]) {
                runner->vitaWeaponFingerprint = fingerprints[0];
                runner->vitaArmorFingerprint = fingerprints[1];
                runner->vitaEquipmentFingerprintStablePolls = 1;
            } else if (++runner->vitaEquipmentFingerprintStablePolls >= 4) {
                // Four 30-frame polls provide a stable post-load baseline while
                // keeping genuine equipment changes responsive afterwards.
                runner->vitaEquipmentFingerprintInitialized = true;
                VitaProbe_logLine("TROPHY_BRIDGE=equipment_baseline_ready");
            }
        } else {
            if (runner->vitaWeaponFingerprint != fingerprints[0]) {
                runner->vitaWeaponFingerprint = fingerprints[0];
                unlocked[26] = true;
                VitaProbe_logLine("TROPHY_BRIDGE=weapon_change id=26");
            }
            if (runner->vitaArmorFingerprint != fingerprints[1]) {
                runner->vitaArmorFingerprint = fingerprints[1];
                int32_t chapterVar = VM_getOrAllocateVarID(runner->vmContext, "chapter");
                int32_t chapter = RValue_toInt32(Instance_getSelfVar(
                    runner->vmContext->globalScopeInstance, chapterVar));
                if (chapter < 1 || chapter > 4) chapter = 2;
                int32_t armorTrophy = 13 + chapter;
                unlocked[armorTrophy] = true;
                char detail[64];
                snprintf(detail, sizeof(detail), "TROPHY_BRIDGE=armor_change id=%d chapter=%d",
                         armorTrophy, chapter);
                VitaProbe_logLine(detail);
            }
        }

    }
#endif
}

// Finds the index for the first event larger than or equal to the timeStamp
static uint32_t Timeline_findLarger(Timeline* timeline, float timeStamp) {
    if (timeline->momentCount == 0)
        return 0;

    // Will happen very often so we first check last
    if (timeStamp > (float) timeline->moments[timeline->momentCount - 1].step)
        return timeline->momentCount;

    repeat(timeline->momentCount, i) {
        if ((float) timeline->moments[i].step >= timeStamp) return (uint32_t) i;
    }
    return timeline->momentCount;
}

// Finds the index for the last event smaller than or equal to _timeStamp
static int32_t Timeline_findSmaller(Timeline* timeline, float timeStamp) {
    if (timeline->momentCount == 0)
        return 0;

    // Will happen very often so we first check last
    if ((float) timeline->moments[0].step > timeStamp)
        return -1;

    for (int32_t i = (int32_t) timeline->momentCount - 1; 0 <= i; i--) {
        if ((float) timeline->moments[i].step <= timeStamp) return i;
    }
    return -1;
}

// See GameMaker-HTML5's "HandleTimeLine" for reference
static void tickTimelines(Runner* runner) {
    Tmln* tmln = &runner->dataWin->tmln;
    // Fast path: if the data.win doesn't have any timelines, we don't need to snapshot instances
    if (tmln->count == 0)
        return;

    int32_t n = (int32_t) arrlen(runner->instances);
    if (n == 0)
        return;

    // Snapshot the live instance list so moment code that calls instance_create/destroy doesn't mutate what we iterate.
    int32_t snapBase = (int32_t) arrlen(runner->instanceSnapshots);
    arrsetlen(runner->instanceSnapshots, snapBase + n);
    memcpy(&runner->instanceSnapshots[snapBase], runner->instances, (size_t) n * sizeof(Instance*));

    repeat(n, i) {
        Instance* inst = runner->instanceSnapshots[snapBase + i];
        if (!inst->active || inst->destroyed)
            continue;

        int32_t idx = inst->timelineIndex;
        if (0 > idx)
            continue;

        if ((uint32_t) idx >= tmln->count)
            continue;

        if (!inst->timelineRunning)
            continue;

        Timeline* timeline = &tmln->timelines[idx];
        if (!timeline->present || timeline->momentCount == 0)
            continue;

        float maxMoment = (float) timeline->moments[timeline->momentCount - 1].step;

        if (inst->timelineSpeed > 0.0f) {
            // Forward sweep: fire moments in [oldPos, newPos)
            uint32_t ind1 = Timeline_findLarger(timeline, inst->timelinePosition);
            inst->timelinePosition += inst->timelineSpeed;
            uint32_t ind2 = Timeline_findLarger(timeline, inst->timelinePosition);

            for (uint32_t j = ind1; ind2 > j; j++) {
                TimelineMoment* moment = &timeline->moments[j];
                repeat(moment->actionCount, a) {
                    executeCode(runner, inst, moment->actions[a].codeId);
                }
            }

            if (inst->timelineLoop && inst->timelinePosition > maxMoment) {
                inst->timelinePosition = 0.0f;
            }
        } else {
            // Backward sweep (also covers timelineSpeed == 0: ind1 == ind2, no events, no position change): fire moments in (newPos, oldPos]
            int32_t ind1 = Timeline_findSmaller(timeline, inst->timelinePosition);
            inst->timelinePosition += inst->timelineSpeed;
            int32_t ind2 = Timeline_findSmaller(timeline, inst->timelinePosition);

            for (int32_t j = ind1; j > ind2; j--) {
                TimelineMoment* moment = &timeline->moments[j];
                repeat(moment->actionCount, a) {
                    executeCode(runner, inst, moment->actions[a].codeId);
                }
            }

            if (inst->timelineLoop && 0.0f > inst->timelinePosition) {
                inst->timelinePosition = maxMoment;
            }
        }
    }

    arrsetlen(runner->instanceSnapshots, snapBase);
}

#ifdef PLATFORM_VITA
static Instance* vitaFirstActiveObjectInstance(Runner* runner, int32_t objectIndex) {
    if (runner == nullptr || objectIndex < 0 ||
        objectIndex >= (int32_t)runner->dataWin->objt.count) return nullptr;
    Instance** bucket = runner->instancesByObject[objectIndex];
    for (int32_t i = 0; i < (int32_t)arrlen(bucket); ++i)
        if (bucket[i] != nullptr && bucket[i]->active) return bucket[i];
    return nullptr;
}

static int32_t vitaCountActiveObjectInstances(Runner* runner, int32_t objectIndex) {
    if (runner == nullptr || objectIndex < 0 ||
        objectIndex >= (int32_t)runner->dataWin->objt.count) return 0;
    int32_t count = 0;
    Instance** bucket = runner->instancesByObject[objectIndex];
    for (int32_t i = 0; i < (int32_t)arrlen(bucket); ++i)
        if (bucket[i] != nullptr && bucket[i]->active) count++;
    return count;
}

static int32_t vitaInstanceInt(Instance* inst, int32_t varId) {
    return inst != nullptr && varId >= 0 ?
        RValue_toInt32(Instance_getSelfVar(inst, varId)) : 0;
}

static void vitaCityMiceSoftlockGuard(Runner* runner) {
    static uint32_t staleWaveFrames = 0;
    if (runner == nullptr || runner->currentRoom == nullptr || runner->currentRoom->name == nullptr ||
        strncmp(runner->currentRoom->name, "room_dw_city_mice", 17) != 0) {
        staleWaveFrames = 0;
        return;
    }

    VMContext* ctx = runner->vmContext;
    int32_t switchObj = shget(runner->assetsByName, "obj_mouseSpawnSwitch");
    if (switchObj < 0) switchObj = shget(runner->assetsByName, "obj_mice2Switch");
    if (switchObj < 0) switchObj = shget(runner->assetsByName, "obj_mice3_platSwitch");

    int32_t mouseObj = shget(runner->assetsByName, "obj_holemouse");
    int32_t scaredNoelleObj = shget(runner->assetsByName, "obj_noelle_scared");

    int32_t generatorObj = shget(runner->assetsByName, "obj_holemouse_generator");
    if (generatorObj < 0) generatorObj = shget(runner->assetsByName, "obj_lottery_holemouse_generator");

    int32_t dialogObj = shget(runner->assetsByName, "obj_dialoguer");
    int32_t cutsceneObj = shget(runner->assetsByName, "obj_cutscene_master");

    Instance* sw = vitaFirstActiveObjectInstance(runner, switchObj);
    Instance* generator = vitaFirstActiveObjectInstance(runner, generatorObj);

    // obj_noelle_scared spawns a fresh obj_caterpillarchara on landing without setting name="noelle".
    // Restore name and target properties so subsequent mice collisions trigger Noelle's jump.
    int32_t caterpId = shget(runner->assetsByName, "obj_caterpillarchara");
    if (caterpId >= 0) {
        int32_t nameVarId = shget(ctx->varNameMap, "name");
        int32_t targetVarId = shget(ctx->varNameMap, "target");
        for (ptrdiff_t i = 0; i < arrlen(runner->instances); ++i) {
            Instance* catInst = runner->instances[i];
            if (catInst != nullptr && catInst->objectIndex == caterpId && catInst->active && !catInst->destroyed) {
                RValue nameVal = Instance_getSelfVar(catInst, nameVarId);
                if (nameVal.type != RVALUE_STRING || nameVal.string == nullptr || strlen(nameVal.string) == 0) {
                    Instance_setSelfVar(catInst, nameVarId, RValue_makeString("noelle"));
                }
                RValue targetVal = Instance_getSelfVar(catInst, targetVarId);
                if (targetVal.type == RVALUE_UNDEFINED || (targetVal.type == RVALUE_INT32 && targetVal.int32 <= 0)) {
                    Instance_setSelfVar(catInst, targetVarId, RValue_makeInt32(1));
                }
            }
        }
    }

    // Between waves obj_noelle_scared persists with stale 'con' (e.g. con=4 from the landing phase).
    // When the next wave of mice arrives and a holemouse hits Noelle, the Collision handler fires
    // alarm[0]=1 correctly, but Alarm_0 checks 'con == 0' before starting the jump — so if 'con'
    // is left non-zero from the previous wave the jump animation is silently skipped.
    // Fix: while mice are spawning/active and Noelle is NOT mid-jump (con != 1/2/3), reset the
    // scared Noelle instance back to its ready state so the next collision works properly.
    if (scaredNoelleObj >= 0) {
        int32_t conVarId        = shget(ctx->varNameMap, "con");
        int32_t gracePeriodVarId = shget(ctx->varNameMap, "graceperiod");
        int32_t jumpingVarId    = shget(ctx->varNameMap, "jumping");
        int32_t jumpyVarId      = shget(ctx->varNameMap, "jumpy");
        int32_t jumpySpeedVarId = shget(ctx->varNameMap, "jumpyspeed");
        int32_t timerVarId2     = shget(ctx->varNameMap, "timer");
        int32_t mouseCount      = vitaCountActiveObjectInstances(runner, mouseObj);

        for (ptrdiff_t i = 0; i < arrlen(runner->instances); ++i) {
            Instance* noelleInst = runner->instances[i];
            if (noelleInst == nullptr || noelleInst->objectIndex != scaredNoelleObj ||
                !noelleInst->active || noelleInst->destroyed) continue;

            int32_t con = conVarId >= 0 ? RValue_toInt32(Instance_getSelfVar(noelleInst, conVarId)) : 0;
            // con==1: running to mouse, con==2: jumping up, con==3: falling down — do NOT interrupt.
            // con==0: idle/waiting, con==4+: post-landing states — safe to reset between waves.
            bool midJump = (con >= 1 && con <= 3);
            if (!midJump && mouseCount > 0) {
                // Reset to idle so the Collision handler and Alarm_0 work on the next hit.
                if (conVarId >= 0)         Instance_setSelfVar(noelleInst, conVarId,        RValue_makeInt32(0));
                if (gracePeriodVarId >= 0) Instance_setSelfVar(noelleInst, gracePeriodVarId, RValue_makeInt32(0));
                if (jumpingVarId >= 0)     Instance_setSelfVar(noelleInst, jumpingVarId,     RValue_makeInt32(0));
                if (jumpyVarId >= 0)       Instance_setSelfVar(noelleInst, jumpyVarId,       RValue_makeInt32(0));
                if (jumpySpeedVarId >= 0)  Instance_setSelfVar(noelleInst, jumpySpeedVarId,  RValue_makeInt32(0));
                if (timerVarId2 >= 0)      Instance_setSelfVar(noelleInst, timerVarId2,      RValue_makeInt32(0));
            }
        }
    }

    int32_t timerId = shget(ctx->varNameMap, "timer");
    int32_t mouseCreateId = shget(ctx->varNameMap, "mousecreate");
    int32_t genTimerId = shget(ctx->varNameMap, "gentimer");
    int32_t interactId = shget(ctx->varNameMap, "interact");

    int32_t mouseCreate = generator != nullptr ? vitaInstanceInt(generator, mouseCreateId) : 0;
    int32_t genTimer = generator != nullptr ? vitaInstanceInt(generator, genTimerId) : 0;
    int32_t scaredNoelleCount = vitaCountActiveObjectInstances(runner, scaredNoelleObj);
    int32_t dialogs = vitaCountActiveObjectInstances(runner, dialogObj);
    int32_t cutscenes = vitaCountActiveObjectInstances(runner, cutsceneObj);
    int32_t interact = vitaInstanceInt(ctx->globalScopeInstance, interactId);

    // Watchdog ONLY triggers if Noelle is not currently performing her scared jump animation
    // and controls have been locked with no active dialogs/cutscenes for more than 180 frames (3 seconds).
    bool staleLock = interact == 1 && mouseCreate == 0 && genTimer <= 0 &&
                     dialogs == 0 && cutscenes == 0 && scaredNoelleCount == 0;

    staleWaveFrames = staleLock ? staleWaveFrames + 1U : 0U;

    if (staleWaveFrames >= 180U) {
        int32_t removed = 0;
        Instance* staleMouse = nullptr;
        while ((staleMouse = vitaFirstActiveObjectInstance(runner, mouseObj)) != nullptr) {
            Runner_destroyInstance(runner, staleMouse, true);
            removed++;
        }
        Instance_setSelfVar(ctx->globalScopeInstance, interactId, RValue_makeInt32(0));
        if (sw != nullptr) Instance_setSelfVar(sw, timerId, RValue_makeInt32(61));
        char line[128];
        snprintf(line, sizeof(line),
                 "MICE_SOFTLOCK_GUARD=released_stale_lock room=%s removed=%d",
                 runner->currentRoom->name, removed);
        extern void VitaProbe_logLine(const char* text);
        VitaProbe_logLine(line);
        staleWaveFrames = 0;
    }
}
#endif

void Runner_step(Runner* runner) {
    uint64_t stepStartNanos = nowNanos();
    runner->profStepEventsUs = 0;
    runner->profStepAlarmsUs = 0;
    runner->profStepSpatialUs = 0;
    runner->profStepCollisionUs = 0;
    runner->profStepOtherUs = 0;
#ifdef PLATFORM_VITA
    if (runner->pendingRoom >= 0 && runner->vitaRoomTransitionWaiting) {
        Runner_handlePendingRoomChange(runner);
        if (runner->pendingRoom >= 0) return;
    }
#endif
    // The snapshot arena is stack-like and every push must be matched with a pop within the same frame. Assert that invariant at the top of each step: a non-zero length here means some site below pushed without popping, and we want a loud failure with the offending length so we can find it instead of silently leaking until the next frame.
    requireMessageFormatted(__FILE__, __LINE__, arrlen(runner->instanceSnapshots) == 0, "instanceSnapshots arena was not fully popped at end of previous frame (length=%td)", arrlen(runner->instanceSnapshots));

    // Save xprevious/yprevious and path_positionprevious for all active instances
    int32_t prevCount = (int32_t) arrlen(runner->instances);
    repeat(prevCount, i) {
        Instance* inst = runner->instances[i];
        if (inst->active) {
            inst->xprevious = inst->x;
            inst->yprevious = inst->y;
            inst->pathPositionPrevious = inst->pathPosition;
        }
    }

    // Advance image_index by image_speed for all active instances
    // TODO: Newer GameMaker versions (not sure exactly which, but at least GM 2024 does this) defers Animation End: have Instance_Animate just set a per-instance "wrapped" flag, and dispatch the event via a new ProcessSpriteMessageEvents step between Step and the motion loop!
    int32_t animCount = (int32_t) arrlen(runner->instances);
    int32_t animEndSlot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_OTHER, OTHER_ANIMATION_END);
    repeat(animCount, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;
        if (0 > inst->spriteIndex) {
            inst->imageIndex += inst->imageSpeed;
            continue;
        }
        // Wrap image_index (matches HTML5 runner: manual subtract/add instead of using fmod)
        Sprite* sprite = &runner->dataWin->sprt.sprites[inst->spriteIndex];
        if (sprite->specialType == true) {
            if (DataWin_isVersionAtLeast(runner->dataWin, 2, 0, 0, 0)) {
                if (sprite->gms2PlaybackSpeedType == true) {
                    inst->imageIndex += inst->imageSpeed * sprite->gms2PlaybackSpeed;
                } else {
                    inst->imageIndex += (1.0/runner->currentRoom->speed) * sprite->gms2PlaybackSpeed * inst->imageSpeed;
                }   
            }
        } else {
            inst->imageIndex += inst->imageSpeed;    
        }
        float frameCount = (float) sprite->textureCount;
        bool wrapped = false;
        if (inst->imageIndex >= frameCount) {
            inst->imageIndex -= frameCount;
            wrapped = true;
        } else if (0.0f > inst->imageIndex) {
            inst->imageIndex += frameCount;
            wrapped = true;
        }
        if (wrapped && animEndSlot >= 0) {
            int32_t ownerObjectIndex = -1;
            int32_t codeId = ResolvedEventTable_lookup(&runner->eventTable, inst->objectIndex, animEndSlot, &ownerObjectIndex);
            if (codeId >= 0) Runner_executeResolvedEvent(runner, inst, EVENT_OTHER, OTHER_ANIMATION_END, codeId, ownerObjectIndex);
        }
    }

    // Scroll backgrounds
    Runner_scrollBackgrounds(runner);

    // Advance GMS2 layer parallax (hspeed/vspeed per frame)
    size_t layerCount = arrlenu(runner->runtimeLayers);
    uint64_t tileTimeUs = (nowNanos() - runner->gameStartTime) / 1000;
    repeat(layerCount, i) {
        RuntimeLayer* rl = &runner->runtimeLayers[i];
        rl->xOffset += rl->hSpeed;
        rl->yOffset += rl->vSpeed;
        for (size_t j = 0; j < arrlenu(rl->elements); ++j) {
            RuntimeLayerElement* el = &rl->elements[j];
            if (el->type != RuntimeLayerElementType_Tilemap || el->tilemapData == nullptr) continue;
            int32_t bg = el->tilemapData->backgroundIndex;
            if (bg >= 0 && (uint32_t)bg < runner->dataWin->bgnd.count)
                el->tilemapFrame = Runner_tileAnimationFrame(&runner->dataWin->bgnd.backgrounds[bg], tileTimeUs, el->tilemapFrame);
        }
    }

    uint64_t t1 = nowNanos();
    // Execute Begin Step for all instances
    Runner_executeEventForAll(runner, EVENT_STEP, STEP_BEGIN);
    uint64_t t2 = nowNanos();
    runner->profStepEventsUs += (t2 - t1) / 1000;
#ifdef NATIVE_GXM_PROBE_VERSION
    runner->gxmStepDispatchUs[0]+=(t2 - t1)/1000;
#endif

    // Process alarms. Outer loop is over alarm slots (matching the native runner's HandleAlarm), and for each slot we walk only the objects in the event table's bySlot range and only those objects' exact instance buckets. Idle instances are further skipped via activeAlarmMask.
    repeat(GML_ALARM_COUNT, alarmIdx) {
        int32_t alarmSlot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_ALARM, alarmIdx);
        if (0 > alarmSlot) continue;
        ResolvedEventTable* table = &runner->eventTable;
        uint32_t entryCount;
        SlotResponderEntry* entries = ResolvedEventTable_slotEntries(table, alarmSlot, &entryCount);

        repeat(entryCount, s) {
            int32_t objIdx = entries[s].concreteObjectId;
            Instance** bucket = runner->instancesByExactObject[objIdx];
            int32_t bucketCount = (int32_t) arrlen(bucket);
            if (bucketCount == 0) continue;

            // All instances in the bucket share the same exact objectIndex, so the handler resolves to one (codeId, owner).
            int32_t ownerObjectIndex = -1;
            int32_t codeId = ResolvedEventTable_lookup(table, objIdx, alarmSlot, &ownerObjectIndex);
            if (0 > codeId) continue;

            // Snapshot only responders whose selected alarm is active.  The
            // previous implementation copied every instance in every object
            // bucket for every alarm slot, even when none of those instances
            // had that alarm enabled. Particle-heavy rooms such as
            // room_dw_mansion_fire_paintings consequently spent ~400 ms in
            // alarm processing. Keep the mutation-safe snapshot semantics,
            // but compact it to actual responders first.
            int32_t snapshotBase = (int32_t) arrlen(runner->instanceSnapshots);
            uint16_t bit = (uint16_t) (1u << alarmIdx);
            repeat(bucketCount, i) {
                Instance* inst = bucket[i];
                if (inst->active && (inst->activeAlarmMask & bit) != 0)
                    arrput(runner->instanceSnapshots, inst);
            }
            int32_t responderCount = (int32_t)arrlen(runner->instanceSnapshots) - snapshotBase;

            repeat(responderCount, i) {
                Instance* inst = runner->instanceSnapshots[snapshotBase + i];
                if (!inst->active) continue;
                if ((inst->activeAlarmMask & bit) == 0) continue;

#ifdef ENABLE_VM_TRACING
                GameObject* object = &runner->dataWin->objt.objects[inst->objectIndex];
                if (shgeti(runner->vmContext->alarmsToBeTraced, "*") != -1 || shgeti(runner->vmContext->alarmsToBeTraced, object->name) != -1) {
                    fprintf(stderr, "VM: [%s] Ticking down Alarm[%d] (instanceId=%d), current tick is %d\n", object->name, (int)alarmIdx, inst->instanceId, inst->alarm[alarmIdx]);
                }
#endif

                inst->alarm[alarmIdx]--;
                if (inst->alarm[alarmIdx] == 0) {
                    inst->alarm[alarmIdx] = -1;
                    inst->activeAlarmMask &= (uint16_t) ~bit;

#ifdef ENABLE_VM_TRACING
                    if (shgeti(runner->vmContext->alarmsToBeTraced, "*") != -1 || shgeti(runner->vmContext->alarmsToBeTraced, object->name) != -1) {
                        fprintf(stderr, "VM: [%s] Firing Alarm[%d] (instanceId=%d)\n", object->name, (int)alarmIdx, inst->instanceId);
                    }
#endif

                    Runner_executeResolvedEvent(runner, inst, EVENT_ALARM, alarmIdx, codeId, ownerObjectIndex);
                }
            }

            arrsetlen(runner->instanceSnapshots, snapshotBase);
        }
    }
    uint64_t t3 = nowNanos();
    runner->profStepAlarmsUs += (t3 - t2) / 1000;

    RunnerKeyboardState* kb = runner->keyboard;
    for (int32_t key = 0; GML_KEY_COUNT > key; key++) {
        if (kb->keyDown[key]) {
            Runner_executeEventForAll(runner, EVENT_KEYBOARD, key);
        }
        if (kb->keyPressed[key]) {
            Runner_executeEventForAll(runner, EVENT_KEYPRESS, key);
        }
        if (kb->keyReleased[key]) {
            Runner_executeEventForAll(runner, EVENT_KEYRELEASE, key);
        }
    }

    if (RunnerKeyboard_check(kb, VK_ANYKEY)) Runner_executeEventForAll(runner, EVENT_KEYBOARD, VK_ANYKEY);
    if (RunnerKeyboard_checkPressed(kb, VK_ANYKEY)) Runner_executeEventForAll(runner, EVENT_KEYPRESS, VK_ANYKEY);
    if (RunnerKeyboard_checkReleased(kb, VK_ANYKEY)) Runner_executeEventForAll(runner, EVENT_KEYRELEASE, VK_ANYKEY);

    if (RunnerKeyboard_check(kb, VK_NOKEY)) Runner_executeEventForAll(runner, EVENT_KEYBOARD, VK_NOKEY);
    if (RunnerKeyboard_checkPressed(kb, VK_NOKEY)) Runner_executeEventForAll(runner, EVENT_KEYPRESS, VK_NOKEY);
    if (RunnerKeyboard_checkReleased(kb, VK_NOKEY)) Runner_executeEventForAll(runner, EVENT_KEYRELEASE, VK_NOKEY);

    // Tick timelines
    tickTimelines(runner);

    dispatchMouseEvents(runner);
    if (runner->pendingRoom >= 0) { Runner_handlePendingRoomChange(runner); return; }

    uint64_t t4 = nowNanos();
    // Execute Normal Step for all instances
    Runner_executeEventForAll(runner, EVENT_STEP, STEP_NORMAL);
    uint64_t t5 = nowNanos();
    runner->profStepEventsUs += (t5 - t4) / 1000;
#ifdef NATIVE_GXM_PROBE_VERSION
    runner->gxmStepDispatchUs[1]+=(t5 - t4)/1000;
#endif

    // Apply motion: friction, gravity, then x += hspeed, y += vspeed
    int32_t motionCount = (int32_t) arrlen(runner->instances);
    int32_t endOfPathSlot = EventSlotMap_lookup(&runner->eventSlotMap, EVENT_OTHER, OTHER_END_OF_PATH);
    repeat(motionCount, mi) {
        Instance* inst = runner->instances[mi];
        if (!inst->active) continue;

        // Friction: reduce speed toward zero (HTML5: AdaptSpeed)
        if (inst->friction != 0.0f) {
            float ns = (inst->speed > 0.0f) ? inst->speed - inst->friction : inst->speed + inst->friction;
            if ((inst->speed > 0.0f && ns < 0.0f) || (inst->speed < 0.0f && ns > 0.0f)) {
                inst->speed = 0.0f;
            } else if (inst->speed != 0.0f) {
                inst->speed = ns;
            }
            Instance_computeComponentsFromSpeed(inst);
        }

        // Gravity: add velocity in gravity_direction (HTML5: AddTo_Speed)
        if (inst->gravity != 0.0f) {
            GMLReal gravDirRad = inst->gravityDirection * (M_PI / 180.0);
            inst->hspeed += (float) (inst->gravity * clampFloat(GMLReal_cos(gravDirRad)));
            inst->vspeed -= (float) (inst->gravity * clampFloat(GMLReal_sin(gravDirRad)));
            Instance_computeSpeedFromComponents(inst);
        }

        // Path adaptation (HTML5: Adapt_Path, runs after friction/gravity, before x+=hspeed)
        if (adaptPath(runner, inst) && endOfPathSlot >= 0) {
            int32_t ownerObjectIndex = -1;
            int32_t codeId = ResolvedEventTable_lookup(&runner->eventTable, inst->objectIndex, endOfPathSlot, &ownerObjectIndex);
            if (codeId >= 0) Runner_executeResolvedEvent(runner, inst, EVENT_OTHER, OTHER_END_OF_PATH, codeId, ownerObjectIndex);
        }

        // Apply movement
        if (inst->hspeed != 0.0f || inst->vspeed != 0.0f) {
            inst->x += inst->hspeed;
            inst->y += inst->vspeed;
            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
        }
    }

    // Dispatch outside room events
    dispatchOutsideRoomEvents(runner);
    repeat(MAX_VIEWS, viewIndex) {
        dispatchOutsideViewEvents(runner, viewIndex);
    }

    for (int i = 0; MAX_GAMEPADS > i; i++) {
        GamepadSlot* slot = &runner->gamepads->slots[i];
        if (slot->connected != slot->connectedPrev) {
            DsMapEntry* map = nullptr;
            arrput(runner->dsMapPool, map);
            int32_t mapId = arrlen(runner->dsMapPool) - 1;

            DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
            shput(*mapPtr, safeStrdup("event_type"), RValue_makeOwnedString(safeStrdup(slot->connected ? "gamepad discovered" : "gamepad lost")));
            shput(*mapPtr, safeStrdup("pad_index"), RValue_makeReal((GMLReal) i));

            runner->asyncLoadMapId = mapId;
            Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ASYNC_SYSTEM);

            // Clean up ds_map
            mapPtr = &runner->dsMapPool[mapId];
            if (*mapPtr != nullptr) {
                repeat(shlen(*mapPtr), j) {
                    free((*mapPtr)[j].key);
                    RValue_free(&(*mapPtr)[j].value);
                }
                shfree(*mapPtr);
                *mapPtr = nullptr;
            }
            runner->asyncLoadMapId = -1;
        }
    }

    // Resolve a pending Xbox One account-picker request
    if (runner->xboxAccountPickerPendingId >= 0) {
        DsMapEntry* map = nullptr;
        arrput(runner->dsMapPool, map);
        int32_t mapId = arrlen(runner->dsMapPool) - 1;

        DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
        shput(*mapPtr, safeStrdup("id"), RValue_makeReal((GMLReal) runner->xboxAccountPickerPendingId));
        shput(*mapPtr, safeStrdup("user"), RValue_makeReal(1.0));
        shput(*mapPtr, safeStrdup("pad_index"), RValue_makeReal((GMLReal) runner->xboxAccountPickerPadIndex));

        runner->asyncLoadMapId = mapId;
        runner->xboxAccountPickerPendingId = -1;
        Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ASYNC_DIALOG);

        // Clean up ds_map
        mapPtr = &runner->dsMapPool[mapId];
        if (*mapPtr != nullptr) {
            repeat(shlen(*mapPtr), j) {
                free((*mapPtr)[j].key);
                RValue_free(&(*mapPtr)[j].value);
            }
            shfree(*mapPtr);
            *mapPtr = nullptr;
        }
        runner->asyncLoadMapId = -1;
    }

    // Fire pending async buffer save/load completions.
    // Copy the queue first so that new async loads aren't done in the list we are currently iterating.
    if (runner->asyncSaveLoadQueue != nullptr) {
        AsyncSaveLoadCompletion* pending = runner->asyncSaveLoadQueue;
        runner->asyncSaveLoadQueue = nullptr;
        repeat((int32_t) arrlen(pending), idx) {
            AsyncSaveLoadCompletion completion = pending[idx];

            DsMapEntry* map = nullptr;
            arrput(runner->dsMapPool, map);
            int32_t mapId = arrlen(runner->dsMapPool) - 1;

            DsMapEntry** mapPtr = &runner->dsMapPool[mapId];
            shput(*mapPtr, safeStrdup("id"), RValue_makeReal((GMLReal) completion.requestId));
            shput(*mapPtr, safeStrdup("status"), RValue_makeReal((GMLReal) completion.status));
            shput(*mapPtr, safeStrdup("error"), RValue_makeReal((GMLReal) completion.error));

            runner->asyncLoadMapId = mapId;
            Runner_executeEventForAll(runner, EVENT_OTHER, OTHER_ASYNC_SAVE_LOAD);

            // Clean up ds_map
            mapPtr = &runner->dsMapPool[mapId];
            if (*mapPtr != nullptr) {
                repeat(shlen(*mapPtr), j) {
                    free((*mapPtr)[j].key);
                    RValue_free(&(*mapPtr)[j].value);
                }
                shfree(*mapPtr);
                *mapPtr = nullptr;
            }
            runner->asyncLoadMapId = -1;
        }
        arrfree(pending);
    }

    // Dispatch collision events
    uint64_t tc1 = nowNanos();
    dispatchCollisionEvents(runner);
    uint64_t tc2 = nowNanos();
    runner->profStepCollisionUs = (tc2 - tc1) / 1000;

    // Execute End Step for all instances
    uint64_t t6 = nowNanos();
    Runner_executeEventForAll(runner, EVENT_STEP, STEP_END);
    uint64_t t7 = nowNanos();
    runner->profStepEventsUs += (t7 - t6) / 1000;
#ifdef NATIVE_GXM_PROBE_VERSION
    runner->gxmStepDispatchUs[2]+=(t7 - t6)/1000;
#endif

#ifdef PLATFORM_VITA
    vitaCityMiceSoftlockGuard(runner);
#endif

    uint64_t totalStepUs = (nowNanos() - stepStartNanos) / 1000;
    uint64_t trackedUs = runner->profStepEventsUs + runner->profStepAlarmsUs + runner->profStepSpatialUs + runner->profStepCollisionUs;
    if (totalStepUs > trackedUs) runner->profStepOtherUs = totalStepUs - trackedUs;
    else runner->profStepOtherUs = 0;

    // Update view following
    updateViews(runner);

    Runner_cleanupDestroyedInstances(runner);
    Runner_sweepDeadStructs(runner);

    runner->frameCount++;
}

// ===[ Surface Stack ]===
// In GameMaker, surfaces are handled via a stack:
// * surface_set_target is like a "push"
// * surface_reset_target is like a "pop"
// * The top surface is the one that gets rendered to

static int32_t findFreeStackSlot(Runner* runner) {
    repeat(MAX_SURFACES, i) {
        if (runner->surfaceStack[i] == -1) return i;
    }
    return -1;
}

static int32_t findStackTop(Runner* runner) {
    for (int32_t i = MAX_SURFACES - 1; i >= 0; i--) {
        if (runner->surfaceStack[i] != -1) return i;
    }
    return -1;
}

void Runner_guiSizeChanged(Runner* runner) {
    if (!runner->inGuiPass || runner->renderer == nullptr) return;
    int32_t guiW = runner->guiWidth > 0 ? runner->guiWidth : runner->guiPassPortW;
    int32_t guiH = runner->guiHeight > 0 ? runner->guiHeight : runner->guiPassPortH;
    runner->guiPassW = guiW;
    runner->guiPassH = guiH;
    int32_t top = findStackTop(runner);
    bool renderingToUserSurface = (top != -1 && runner->surfaceStack[top] != runner->applicationSurfaceId);
    runner->renderer->vtable->setGuiProjection(runner->renderer, guiW, guiH, runner->guiPassPortW, runner->guiPassPortH, renderingToUserSurface);
}

bool Runner_surfaceSetTarget(Runner* runner, int32_t surfaceID) {
    if (runner->renderer == nullptr) return false;

    int32_t slot = findFreeStackSlot(runner);
    if (slot == -1) return false;

    runner->surfaceStack[slot] = surfaceID;
    runner->renderer->vtable->flush(runner->renderer);
    return runner->renderer->vtable->setRenderTarget(runner->renderer, surfaceID, false);
}

bool Runner_surfaceResetTarget(Runner* runner) {
    if (runner->renderer == nullptr) return false;

    int32_t top = findStackTop(runner);
    if (top == -1) return false;

    runner->surfaceStack[top] = -1;
    runner->renderer->vtable->flush(runner->renderer);

    int32_t newTop = findStackTop(runner);
    int32_t newTarget = newTop == -1 ? runner->applicationSurfaceId : runner->surfaceStack[newTop];
    runner->renderer->vtable->setRenderTarget(runner->renderer, newTarget, newTop == -1);
    if (newTop == -1 && runner->inGuiPass) {
        // Inside Pre Draw / Post Draw / Draw GUI the base target is the GUI pass target with the GUI projection, not the room view.
        // (See GameMaker-HTML5's g_InGUI_Zone)
        runner->renderer->vtable->beginGUI(runner->renderer, runner->guiPassW, runner->guiPassH, 0, 0, runner->guiPassPortW, runner->guiPassPortH, runner->guiPassTarget);
    }
    return true;
}

int32_t Runner_surfaceGetTarget(Runner* runner) {
    int32_t top = findStackTop(runner);
    if (top == -1) return runner->applicationSurfaceId;
    return runner->surfaceStack[top];
}

void Runner_beginFrame(
    Runner* runner,
    int32_t gameW,
    int32_t gameH,
    int32_t windowW,
    int32_t windowH,
    int32_t framebufferW,
    int32_t framebufferH
) {
    Renderer* renderer = runner->renderer;
#ifdef PLATFORM_VITA
    // Emit only on scene-state changes, not every Draw/particle. Distinguishes
    // duplicated HUD instances from a multi-view/surface composition problem.
    if (g_vitaActiveChapter == 5 && runner->frameCount % 120 == 0) {
        static int lastRoom = -1, lastBars = -1, lastViews = -1, lastPetals = -1;
        int bars = 0, petals = 0, views = 0;
        for (size_t i = 0; i < arrlenu(runner->instances); ++i) {
            Instance* inst = runner->instances[i];
            if (!inst->active || inst->destroyed || inst->objectIndex < 0) continue;
            const char* name = runner->dataWin->objt.objects[inst->objectIndex].name;
            if (!name) continue;
            if (strcmp(name, "obj_tensionbar") == 0) ++bars;
            if (inst->visible && (strcmp(name, "obj_spin_petal") == 0 || strcmp(name, "obj_petal_rain") == 0)) petals = 1;
        }
        for (int i = 0; i < MAX_VIEWS; ++i) if (runner->views[i].enabled) views |= 1 << i;
        if (lastRoom != runner->currentRoomIndex || lastBars != bars || lastViews != views || lastPetals != petals) {
            char line[256];
            snprintf(line, sizeof(line), "CH5_SCENE_STATE room=%s tp_instances=%d view_mask=%d petals_visible=%d",
                     runner->currentRoom ? runner->currentRoom->name : "<none>", bars, views, petals);
            VitaProbe_logLine(line);
            lastRoom = runner->currentRoomIndex; lastBars = bars; lastViews = views; lastPetals = petals;
        }
    }
#endif

    // The application surface (FBO) is sized to defaultWindowWidth x defaultWindowHeight.
    // It is a bit hard to understand, but here's how it works:
    // The Port X/Port Y controls the position of the game viewport within the application surface.
    // The Port W/Port H controls the size of the game viewport within the application surface.
    // Think of it like if you had an image (or... well, a framebuffer) and you are "pasting" it over the application surface.
    // And the Port W/Port H are scaled by the window size too (set by the GEN8 chunk)
    Runner_computeViewDisplayScale(runner, gameW, gameH, &runner->displayScaleX, &runner->displayScaleY);

    // Calculate viewport (letterboxing) in screen coordinates for mouse mapping
    int32_t scaledW, scaledH;
    if ((gameW * windowH) / gameH < windowW) {
        scaledW = (gameW * windowH) / gameH;
        scaledH = windowH;
    } else {
        scaledW = windowW;
        scaledH = (gameH * windowW) / gameW;
    }

    runner->viewportX = (windowW - scaledW) / 2;
    runner->viewportY = (windowH - scaledH) / 2;
    runner->viewportW = scaledW;
    runner->viewportH = scaledH;
    runner->renderGameW = gameW;
    runner->renderGameH = gameH;
    runner->applicationSurfaceId = renderer->vtable->ensureApplicationSurface(renderer, gameW, gameH);
    renderer->vtable->beginFrame(renderer, gameW, gameH, framebufferW, framebufferH);
}

// ===[ State Dump ]===

void Runner_dumpState(Runner* runner) {
    DataWin* dataWin = runner->dataWin;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);

    printf("=== Frame %d State Dump ===\n", runner->frameCount);
    printf("Room: %s (index %d)\n", runner->currentRoom->name, runner->currentRoomIndex);
    printf("Instance count: %d\n", instanceCount);

    repeat(instanceCount, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;

        GameObject* gameObject = nullptr;
        const char* objName = "<unknown>";
        if (inst->objectIndex >= 0 && dataWin->objt.count > (uint32_t) inst->objectIndex) {
            gameObject = &dataWin->objt.objects[inst->objectIndex];
            objName = gameObject->name;
        }

        const char* spriteName = "<none>";
        if (inst->spriteIndex >= 0 && dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
            spriteName = dataWin->sprt.sprites[inst->spriteIndex].name;
        }

        const char* parentName = "<none>";
        if (gameObject != nullptr && gameObject->parentId >= 0 && dataWin->objt.count > (uint32_t) gameObject->parentId) {
            parentName = dataWin->objt.objects[gameObject->parentId].name;
        }

        printf("\n--- Instance #%d (%s, objectIndex=%d) ---\n", inst->instanceId, objName, inst->objectIndex);
        printf("  Position: (%g, %g)\n", (double) inst->x, (double) inst->y);
        printf("  Depth: %d\n", inst->depth);
        printf("  Sprite: %s (index %d), imageIndex=%g, imageSpeed=%g\n", spriteName, inst->spriteIndex, (double) inst->imageIndex, (double) inst->imageSpeed);
        printf("  Scale: (%g, %g), Angle: %g, Alpha: %g, Blend: 0x%06X\n", (double) inst->imageXscale, (double) inst->imageYscale, (double) inst->imageAngle, (double) inst->imageAlpha, inst->imageBlend);
        printf("  Visible: %s, Active: %s, Solid: %s, Persistent: %s\n", inst->visible ? "true" : "false", inst->active ? "true" : "false", inst->solid ? "true" : "false", inst->persistent ? "true" : "false");
        printf("  Parent: %s (parentId=%d)\n", parentName, gameObject != nullptr ? gameObject->parentId : -1);

        // Active alarms
        bool hasAlarm = false;
        repeat(GML_ALARM_COUNT, alarmIdx) {
            if (inst->alarm[alarmIdx] >= 0) {
                if (!hasAlarm) { printf("  Alarms:"); hasAlarm = true; }
                printf(" [%d]=%d", (int)alarmIdx, inst->alarm[alarmIdx]);
            }
        }
        if (hasAlarm) printf("\n");

        // Self variables
        bool hasSelfVars = false;
        bool hasSelfArrays = false;
        repeat(inst->selfVars.capacity, svIdx) {
            IntRValueEntry* entry = &inst->selfVars.entries[svIdx];
            if (entry->key == INT_RVALUE_HASHMAP_EMPTY_KEY) continue;
            int32_t varID = entry->key;
            RValue val = entry->value;
            if (val.type == RVALUE_UNDEFINED) continue;

            const char* varName = "?";
            repeat(dataWin->vari.variableCount, varIdx) {
                Variable* var = &dataWin->vari.variables[varIdx];
                if (var->instanceType == INSTANCE_SELF && var->varID == varID) {
                    varName = var->name;
                    break;
                }
            }

            if (val.type == RVALUE_ARRAY && val.array != nullptr) {
                if (!hasSelfArrays) { printf("  Self Arrays:\n"); hasSelfArrays = true; }
                repeat(GMLArray_length1D(val.array), ai) {
                    RValue* cell = GMLArray_slot(val.array, ai);
                    if (cell == nullptr || cell->type == RVALUE_UNDEFINED) continue;
                    char* innerStr = RValue_toStringFancy(*cell);
                    printf("    %s[%d] = %s\n", varName, (int) ai, innerStr);
                    free(innerStr);
                }
            } else {
                if (!hasSelfVars) { printf("  Self Variables:\n"); hasSelfVars = true; }
                char* valStr = RValue_toStringFancy(val);
                printf("    %s = %s\n", varName, valStr);
                free(valStr);
            }
        }
    }

    // Global variables (non-array)
    printf("\n=== Global Variables ===\n");

    repeat(runner->vmContext->globalScopeInstance->selfVars.capacity, i) {
        IntRValueEntry entryOnTheVarStruct = runner->vmContext->globalScopeInstance->selfVars.entries[i];
        RValue target = VM_structGetVariableByVarId(runner->vmContext->globalScopeInstance, entryOnTheVarStruct.key, -1);

        if (entryOnTheVarStruct.key != INT_RVALUE_HASHMAP_EMPTY_KEY) {
            char* name = VM_getVariableNameByVarId(runner->vmContext, entryOnTheVarStruct.key);

            if (target.type == RVALUE_ARRAY) {
                repeat(GMLArray_length1D(target.array), ai) {
                    RValue* cell = GMLArray_slot(target.array, ai);
                    if (cell == nullptr || cell->type == RVALUE_UNDEFINED) continue;
                    char* innerStr = RValue_toStringFancy(*cell);
                    printf("  %s[%d] = %s\n", name, (int) ai, innerStr);
                    free(innerStr);
                }
            }

            char* valStr = RValue_toStringTyped(target);
            printf("  %s = %s\n", name, valStr);
            free(valStr);
        }
    }

    printf("\n=== End Frame %d State Dump ===\n", runner->frameCount);
}

// ===[ JSON State Dump ]===

static void writeRValueJson(JsonWriter* w, RValue val) {
    switch (val.type) {
        case RVALUE_REAL:
            JsonWriter_double(w, val.real);
            break;
        case RVALUE_INT32:
            JsonWriter_int(w, val.int32);
            break;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64:
            JsonWriter_int(w, val.int64);
            break;
#endif
        case RVALUE_STRING:
            JsonWriter_string(w, val.string);
            break;
        case RVALUE_BOOL:
            JsonWriter_bool(w, val.int32 != 0);
            break;
        case RVALUE_UNDEFINED:
            JsonWriter_null(w);
            break;
        case RVALUE_ARRAY: {
            // Render arrays as a JSON array. Skips RVALUE_UNDEFINED entries (they read as 0/null anyway).
            JsonWriter_beginArray(w);
            if (val.array != nullptr) {
                repeat(GMLArray_length1D(val.array), ai) {
                    RValue* cell = GMLArray_slot(val.array, ai);
                    writeRValueJson(w, cell != nullptr ? *cell : RValue_makeUndefined());
                }
            }
            JsonWriter_endArray(w);
            break;
        }
#if IS_WAD17_OR_HIGHER_ENABLED
        case RVALUE_METHOD: {
            char buf[64];
            snprintf(buf, sizeof(buf), "<method:%d>", val.method->codeIndex);
            JsonWriter_string(w, buf);
            break;
        }
#endif
        case RVALUE_STRUCT: {
            char buf[64];
            snprintf(buf, sizeof(buf), "<struct:%u>", val.structInst != nullptr ? val.structInst->instanceId : 0);
            JsonWriter_string(w, buf);
            break;
        }
        case RVALUE_ASSETREF:
            JsonWriter_int(w, val.int32);
            break;
    }
}

char* Runner_dumpStateJson(Runner* runner) {
    DataWin* dataWin = runner->dataWin;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);

    JsonWriter w = JsonWriter_create();

    JsonWriter_beginObject(&w);

    JsonWriter_propertyInt(&w, "frame", runner->frameCount);

    // Room info
    JsonWriter_key(&w, "room");
    JsonWriter_beginObject(&w);
    JsonWriter_propertyString(&w, "name", runner->currentRoom->name);
    JsonWriter_propertyInt(&w, "index", runner->currentRoomIndex);
    JsonWriter_endObject(&w);

    // Instances
    JsonWriter_key(&w, "instances");
    JsonWriter_beginArray(&w);

    repeat(instanceCount, i) {
        Instance* inst = runner->instances[i];
        if (!inst->active) continue;

        const char* objName = (inst->objectIndex >= 0 && dataWin->objt.count > (uint32_t) inst->objectIndex) ? dataWin->objt.objects[inst->objectIndex].name : nullptr;

        const char* spriteName = nullptr;
        if (inst->spriteIndex >= 0 && dataWin->sprt.count > (uint32_t) inst->spriteIndex) {
            spriteName = dataWin->sprt.sprites[inst->spriteIndex].name;
        }

        JsonWriter_beginObject(&w);

        JsonWriter_propertyInt(&w, "instanceId", inst->instanceId);
        JsonWriter_propertyString(&w, "objectName", objName);
        JsonWriter_propertyInt(&w, "objectIndex", inst->objectIndex);

        // Parent object
        const char* parentName = nullptr;
        int32_t parentId = -1;
        if (inst->objectIndex >= 0 && dataWin->objt.count > (uint32_t) inst->objectIndex) {
            parentId = dataWin->objt.objects[inst->objectIndex].parentId;
            if (parentId >= 0 && dataWin->objt.count > (uint32_t) parentId) {
                parentName = dataWin->objt.objects[parentId].name;
            }
        }
        JsonWriter_propertyString(&w, "parentObjectName", parentName);
        JsonWriter_propertyInt(&w, "parentObjectIndex", parentId);

        JsonWriter_propertyDouble(&w, "x", inst->x);
        JsonWriter_propertyDouble(&w, "y", inst->y);
        JsonWriter_propertyInt(&w, "depth", inst->depth);

        // Sprite
        JsonWriter_key(&w, "sprite");
        JsonWriter_beginObject(&w);
        JsonWriter_propertyString(&w, "name", spriteName);
        JsonWriter_propertyInt(&w, "index", inst->spriteIndex);
        JsonWriter_propertyDouble(&w, "imageIndex", inst->imageIndex);
        JsonWriter_propertyDouble(&w, "imageSpeed", inst->imageSpeed);
        JsonWriter_endObject(&w);

        // Scale
        JsonWriter_key(&w, "scale");
        JsonWriter_beginObject(&w);
        JsonWriter_propertyDouble(&w, "x", inst->imageXscale);
        JsonWriter_propertyDouble(&w, "y", inst->imageYscale);
        JsonWriter_endObject(&w);

        JsonWriter_propertyDouble(&w, "angle", inst->imageAngle);
        JsonWriter_propertyDouble(&w, "alpha", inst->imageAlpha);
        JsonWriter_propertyInt(&w, "blend", inst->imageBlend);
        JsonWriter_propertyBool(&w, "visible", inst->visible);
        JsonWriter_propertyBool(&w, "active", inst->active);
        JsonWriter_propertyBool(&w, "solid", inst->solid);
        JsonWriter_propertyBool(&w, "persistent", inst->persistent);

        // Alarms
        JsonWriter_key(&w, "alarms");
        JsonWriter_beginObject(&w);
        repeat(GML_ALARM_COUNT, alarmIdx) {
            if (inst->alarm[alarmIdx] >= 0) {
                char alarmKey[4];
                snprintf(alarmKey, sizeof(alarmKey), "%d", (int)alarmIdx);
                JsonWriter_propertyInt(&w, alarmKey, inst->alarm[alarmIdx]);
            }
        }
        JsonWriter_endObject(&w);

        // Self variables (non-array, sparse hashmap)
        JsonWriter_key(&w, "selfVariables");
        JsonWriter_beginObject(&w);
        repeat(inst->selfVars.capacity, svIdx) {
            IntRValueEntry* entry = &inst->selfVars.entries[svIdx];
            if (entry->key == INT_RVALUE_HASHMAP_EMPTY_KEY) continue;
            int32_t varID = entry->key;
            RValue val = entry->value;
            if (val.type == RVALUE_UNDEFINED) continue;

            // Resolve variable name from VARI chunk
            const char* varName = "?";
            repeat(dataWin->vari.variableCount, varIdx) {
                Variable* var = &dataWin->vari.variables[varIdx];
                if (var->instanceType == INSTANCE_SELF && var->varID == varID) {
                    varName = var->name;
                    break;
                }
            }

            JsonWriter_key(&w, varName);
            writeRValueJson(&w, val);
        }
        JsonWriter_endObject(&w);
        JsonWriter_endObject(&w);
    }

    JsonWriter_endArray(&w);

    // Tiles
    Room* dumpRoom = runner->currentRoom;
    JsonWriter_key(&w, "tiles");
    JsonWriter_beginArray(&w);
    repeat(dumpRoom->tileCount, tileIdx) {
        RoomTile* tile = &dumpRoom->tiles[tileIdx];
        const char* bgName = (tile->backgroundDefinition >= 0 && dataWin->bgnd.count > (uint32_t) tile->backgroundDefinition) ? dataWin->bgnd.backgrounds[tile->backgroundDefinition].name : nullptr;

        JsonWriter_beginObject(&w);
        JsonWriter_propertyInt(&w, "index", tileIdx);
        JsonWriter_propertyInt(&w, "x", tile->x);
        JsonWriter_propertyInt(&w, "y", tile->y);
        JsonWriter_propertyInt(&w, "backgroundIndex", tile->backgroundDefinition);
        if (bgName != nullptr) {
            JsonWriter_propertyString(&w, "backgroundName", bgName);
        } else {
            JsonWriter_propertyNull(&w, "backgroundName");
        }
        JsonWriter_propertyInt(&w, "sourceX", tile->sourceX);
        JsonWriter_propertyInt(&w, "sourceY", tile->sourceY);
        JsonWriter_propertyInt(&w, "width", tile->width);
        JsonWriter_propertyInt(&w, "height", tile->height);
        JsonWriter_propertyInt(&w, "depth", tile->tileDepth);
        JsonWriter_propertyInt(&w, "instanceID", tile->instanceID);
        JsonWriter_propertyDouble(&w, "scaleX", tile->scaleX);
        JsonWriter_propertyDouble(&w, "scaleY", tile->scaleY);
        JsonWriter_propertyInt(&w, "color", tile->color);

        ptrdiff_t layerIdx = hmgeti(runner->tileLayerMap, tile->tileDepth);
        bool visible = (layerIdx >= 0) ? runner->tileLayerMap[layerIdx].value.visible : true;
        JsonWriter_propertyBool(&w, "visible", visible);
        JsonWriter_endObject(&w);
    }
    JsonWriter_endArray(&w);

    // Global variables (non-array)
    JsonWriter_key(&w, "globalVariables");
    JsonWriter_beginObject(&w);

    repeat(runner->vmContext->globalScopeInstance->selfVars.capacity, i) {
        IntRValueEntry entryOnTheVarStruct = runner->vmContext->globalScopeInstance->selfVars.entries[i];
        RValue target = VM_structGetVariableByVarId(runner->vmContext->globalScopeInstance, entryOnTheVarStruct.key, -1);

        if (entryOnTheVarStruct.key != INT_RVALUE_HASHMAP_EMPTY_KEY) {
            char* name = VM_getVariableNameByVarId(runner->vmContext, entryOnTheVarStruct.key);

            JsonWriter_key(&w, name);
            writeRValueJson(&w, target);
        }
    }

    JsonWriter_endObject(&w);
    JsonWriter_endObject(&w);

    char* result = JsonWriter_copyOutput(&w);
    JsonWriter_free(&w);
    return result;
}

void Runner_free(Runner* runner) {
    if (runner == nullptr) return;

    cleanupState(runner);

    {
        uint32_t objectCount = runner->dataWin->objt.count;
        repeat(objectCount, i) {
            arrfree(runner->instancesByObject[i]);
        }
        free(runner->instancesByObject);
        runner->instancesByObject = nullptr;
    }

    {
        uint32_t objectCount = runner->dataWin->objt.count;
        repeat(objectCount, i) {
            arrfree(runner->instancesByExactObject[i]);
        }
        free(runner->instancesByExactObject);
        runner->instancesByExactObject = nullptr;
    }

    {
        repeat(OBJT_EVENT_TYPE_COUNT, t) {
            arrfree(runner->objectsWithAnyEventOfType[t]);
        }
        free(runner->objectsWithAnyEventOfType);
        runner->objectsWithAnyEventOfType = nullptr;
    }

    {
        repeat(runner->dataWin->objt.count, i) {
            free(runner->flattenedCollisionEvents[i].events);
        }
        free(runner->flattenedCollisionEvents);
        runner->flattenedCollisionEvents = nullptr;
    }
    
    arrfree(runner->cachedDrawables);
    runner->cachedDrawables = nullptr;
    arrfree(runner->instanceSnapshots);
    runner->instanceSnapshots = nullptr;
    arrfree(runner->eventDispatchInstances);
    runner->eventDispatchInstances = nullptr;
    ResolvedEventTable_free(&runner->eventTable);
    EventSlotMap_destroy(&runner->eventSlotMap);
    shfree(runner->assetsByName);

    repeat(arrlen(runner->gameArgs), i) {
        free(runner->gameArgs[i]);
    }
    arrfree(runner->gameArgs);

    RunnerKeyboard_free(runner->keyboard);
    RunnerGamepad_free(runner->gamepads);
    RunnerMouse_free(runner->mouse);
    if (runner->windowTitle)
        free(runner->windowTitle);
    free(runner);
}

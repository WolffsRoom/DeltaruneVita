#include "debug_overlay.h"

#include "collision.h"
#include "real_type.h"
#include "utils.h"
#include "stb_ds.h"
#include <stdio.h>
#include <string.h>

// Colors are in BGR format (0xBBGGRR)
#define MASK_COLOR_AABB_ONLY  0x0000FF // red: bbox only (no precise mask)
#define MASK_COLOR_AABB       0x00FF00 // green: bbox of an instance with a precise mask
#define MASK_COLOR_PIXEL      0xFFFF00 // cyan: a set pixel inside the precise mask
#define MASK_COLOR_SELECTED   0x00FFFF // yellow: highlighted selected object
#define MASK_COLOR_CROSSHAIR  0xFF00FF // magenta: crosshair center

int32_t g_debugSelectedInstanceIndex = -1;

static void DebugOverlay_logInstanceInfo(Runner* runner, Instance* inst, Sprite* spr, InstanceBBox* bbox) {
    if (runner == nullptr || inst == nullptr) return;
    DataWin* dw = runner->dataWin;
    const char* objName = (dw != nullptr && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < dw->objt.count) ?
                           dw->objt.objects[inst->objectIndex].name : "<unknown_obj>";
    const char* sprName = spr != nullptr ? spr->name : "<no_spr>";
    int32_t pageId = -1;
    if (dw != nullptr && spr != nullptr && spr->textureCount > 0 && spr->tpagIndices != nullptr) {
        int32_t tpagIdx = spr->tpagIndices[0];
        if (tpagIdx >= 0 && (uint32_t)tpagIdx < dw->tpag.count)
            pageId = dw->tpag.items[tpagIdx].texturePageId;
    }
    fprintf(stderr, "[OBJ_INSPECT] id=%u obj=%s spr=%s page=%d pos=(%.1f, %.1f) bbox=[%d,%d,%d,%d]\n",
            inst->instanceId, objName, sprName, pageId,
            (double)inst->x, (double)inst->y,
            (int)bbox->left, (int)bbox->top, (int)bbox->right, (int)bbox->bottom);
}

void DebugOverlay_selectNextInstance(Runner* runner, int direction) {
    if (runner == nullptr) return;
    int32_t count = (int32_t)arrlen(runner->instances);
    if (count <= 0) { g_debugSelectedInstanceIndex = -1; return; }
    if (g_debugSelectedInstanceIndex < 0) {
        g_debugSelectedInstanceIndex = direction >= 0 ? 0 : count - 1;
    } else {
        g_debugSelectedInstanceIndex += (direction >= 0 ? 1 : -1);
        if (g_debugSelectedInstanceIndex >= count) g_debugSelectedInstanceIndex = 0;
        if (g_debugSelectedInstanceIndex < 0) g_debugSelectedInstanceIndex = count - 1;
    }
    if (g_debugSelectedInstanceIndex >= 0 && g_debugSelectedInstanceIndex < count) {
        Instance* inst = runner->instances[g_debugSelectedInstanceIndex];
        if (inst != nullptr && inst->active) {
            DataWin* dataWin = runner->dataWin;
            Sprite* spr = Collision_getSprite(dataWin, inst);
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            DebugOverlay_logInstanceInfo(runner, inst, spr, &bbox);
        }
    }
}

/* Which instances the DEV collision map shows. GameMaker collisions ignore
 * image visibility, and most colliders in DELTARUNE are invisible objects
 * (obj_solidblock, obj_soliddark, obj_doorA, obj_trigger...), so visibility
 * must not decide this. An instance without a mask or with an empty box
 * cannot collide. obj_marker is the decoration helper behind scr_marker, also
 * used for full-room fades; it stays hidden in this inspector only, and normal
 * GameMaker collisions and room events remain unmodified. */
static bool DebugOverlay_isSceneInspectable(Runner* runner, Instance* inst,
                                             Sprite* sprite, const InstanceBBox* box,
                                             bool forTouch) {
    if (runner == nullptr || inst == nullptr || !inst->active ||
        box == nullptr || !box->valid ||
        box->right<=box->left || box->bottom<=box->top) return false;
    DataWin* dw=runner->dataWin;
    if(dw != nullptr && inst->objectIndex>=0 &&
       (uint32_t)inst->objectIndex<dw->objt.count) {
        const char* name=dw->objt.objects[inst->objectIndex].name;
        if(name != nullptr && strcmp(name,"obj_marker")==0) return false;
    }
    /* Similar full-room pseudo-layers should not become finger targets, but
     * they are still drawn. Keep small white-pixel interactables inspectable. */
    if(forTouch && sprite != nullptr && sprite->name != nullptr &&
       strcmp(sprite->name,"spr_pixel_white")==0 &&
       ((int64_t)box->right-box->left>=1536 ||
        (int64_t)box->bottom-box->top>=1536)) return false;
    return true;
}

void DebugOverlay_selectInstanceAtPoint(Runner* runner, float worldX, float worldY) {
    if (runner == nullptr) return;
    int32_t count = (int32_t)arrlen(runner->instances);
    int32_t bestIdx=-1;
    uint64_t smallestArea=UINT64_MAX;
    int32_t bestDepth=INT32_MIN;
    repeat(count,i){
        Instance* inst=runner->instances[i];
        if(!inst || !inst->active)continue;
        InstanceBBox bbox=Collision_computeBBox(runner,inst);
        if(!DebugOverlay_isSceneInspectable(runner,inst,
            Collision_getSprite(runner->dataWin,inst),&bbox,true))continue;
        if(worldX < (float)bbox.left || worldX > (float)bbox.right ||
           worldY < (float)bbox.top || worldY > (float)bbox.bottom)continue;
        /* Background masks can cover half the screen, hiding the desired
         * tiny item. Prefer the smallest overlapping drawable bbox, then
         * resolve ties using GameMaker depth (larger depth draws earlier). */
        uint64_t area=(uint64_t)(bbox.right-bbox.left+1)*
                      (uint64_t)(bbox.bottom-bbox.top+1);
        if(area<smallestArea||(area==smallestArea&&inst->depth<bestDepth)){
            smallestArea=area;
            bestDepth=inst->depth;
            bestIdx=(int32_t)i;
        }
    }
    g_debugSelectedInstanceIndex = bestIdx;
    if (bestIdx >= 0 && bestIdx < count) {
        Instance* inst = runner->instances[bestIdx];
        if (inst != nullptr && inst->active) {
            DataWin* dataWin = runner->dataWin;
            Sprite* spr = Collision_getSprite(dataWin, inst);
            InstanceBBox bbox = Collision_computeBBox(runner, inst);
            DebugOverlay_logInstanceInfo(runner, inst, spr, &bbox);
        }
    }
}

void DebugOverlay_drawCollisionMasks(Runner* runner) {
    Renderer* renderer = runner->renderer;
    if (renderer == nullptr) return;

    DataWin* dataWin = runner->dataWin;
    int32_t instanceCount = (int32_t) arrlen(runner->instances);
    if (g_debugSelectedInstanceIndex >= instanceCount) g_debugSelectedInstanceIndex = -1;

    /* Rooms hold thousands of colliders, so skip the ones far from this view.
     * One view of margin covers widescreen expansion; a rotated view or the
     * free camera disables this. */
    GMLCamera* cullCamera = Runner_getCameraForView(runner, (int32_t) runner->viewCurrent);
    float freeCamZoom = runner->freeCamZoom;
    bool cull = cullCamera != nullptr && cullCamera->viewAngle == 0.0f &&
                (freeCamZoom <= 0.0f || freeCamZoom == 1.0f) &&
                runner->freeCamPanX == 0.0f && runner->freeCamPanY == 0.0f;
    float cullLeft = 0.0f, cullTop = 0.0f, cullRight = 0.0f, cullBottom = 0.0f;
    if (cull) {
        cullLeft = (float) cullCamera->viewX - (float) cullCamera->viewWidth;
        cullTop = (float) cullCamera->viewY - (float) cullCamera->viewHeight;
        cullRight = (float) cullCamera->viewX + 2.0f * (float) cullCamera->viewWidth;
        cullBottom = (float) cullCamera->viewY + 2.0f * (float) cullCamera->viewHeight;
    }

    repeat(instanceCount, i) {
        Instance* inst = runner->instances[i];
        if (!inst || !inst->active) continue;
        Sprite* spr = Collision_getSprite(dataWin, inst);
        if (spr == nullptr) continue;
        InstanceBBox bbox = Collision_computeBBox(runner, inst);
        if (!DebugOverlay_isSceneInspectable(runner,inst,spr,&bbox,false))continue;
        bool isSelected = ((int32_t)i == g_debugSelectedInstanceIndex);
        if (cull && !isSelected &&
            ((float) bbox.right < cullLeft || (float) bbox.left > cullRight ||
             (float) bbox.bottom < cullTop || (float) bbox.top > cullBottom)) continue;
        bool hasPreciseMask = Collision_hasFrameMasks(spr);

        if (isSelected) {
            // Highlight selected object in filled yellow + bright yellow outline
            renderer->vtable->drawRectangle(renderer, (float) bbox.left, (float) bbox.top, (float) bbox.right - 1.0f, (float) bbox.bottom - 1.0f, MASK_COLOR_SELECTED, 0.4f, false);
            renderer->vtable->drawRectangle(renderer, (float) bbox.left, (float) bbox.top, (float) bbox.right - 1.0f, (float) bbox.bottom - 1.0f, MASK_COLOR_SELECTED, 1.0f, true);
            // Draw crosshair at instance origin (x, y)
            renderer->vtable->drawRectangle(renderer, (float)inst->x - 4.0f, (float)inst->y - 1.0f, (float)inst->x + 4.0f, (float)inst->y + 1.0f, MASK_COLOR_CROSSHAIR, 1.0f, false);
            renderer->vtable->drawRectangle(renderer, (float)inst->x - 1.0f, (float)inst->y - 4.0f, (float)inst->x + 1.0f, (float)inst->y + 4.0f, MASK_COLOR_CROSSHAIR, 1.0f, false);

            static uint32_t lastLoggedInst = 0xFFFFFFFF;
            if (lastLoggedInst != inst->instanceId) {
                lastLoggedInst = inst->instanceId;
                DebugOverlay_logInstanceInfo(runner, inst, spr, &bbox);
            }
        } else {
            uint32_t outlineColor = hasPreciseMask ? MASK_COLOR_AABB : MASK_COLOR_AABB_ONLY;
            renderer->vtable->drawRectangle(renderer, (float) bbox.left, (float) bbox.top, (float) bbox.right - 1.0f, (float) bbox.bottom - 1.0f, outlineColor, 1.0f, true);
        }
    }

    if (renderer->vtable->flush != nullptr) {
        renderer->vtable->flush(renderer);
    }
}

#if defined(PLATFORM_VITA) || defined(NATIVE_GXM_PROBE_VERSION)
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif
#include <time.h>
#include <sys/stat.h>

static void ensureDirExists(const char* dirPath) {
#if defined(PLATFORM_VITA) || defined(NATIVE_GXM_PROBE_VERSION)
    sceIoMkdir(dirPath, 0777);
#else
#if defined(_WIN32)
    mkdir(dirPath);
#else
    mkdir(dirPath, 0777);
#endif
#endif
}

void DebugOverlay_saveSelectedObjectLog(Runner* runner, int chapter) {
    if (runner == nullptr) return;
    int32_t count = (int32_t)arrlen(runner->instances);
    if (g_debugSelectedInstanceIndex < 0 || g_debugSelectedInstanceIndex >= count) return;

    Instance* inst = runner->instances[g_debugSelectedInstanceIndex];
    if (inst == nullptr || !inst->active) return;

    DataWin* dw = runner->dataWin;
    const char* objName = (dw != nullptr && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < dw->objt.count) ?
                           dw->objt.objects[inst->objectIndex].name : "obj_unknown";
    Sprite* spr = Collision_getSprite(dw, inst);
    const char* sprName = spr != nullptr ? spr->name : "no_sprite";
    int32_t pageId = -1;
    if (dw != nullptr && spr != nullptr && spr->textureCount > 0 && spr->tpagIndices != nullptr) {
        int32_t tpagIdx = spr->tpagIndices[0];
        if (tpagIdx >= 0 && (uint32_t)tpagIdx < dw->tpag.count)
            pageId = dw->tpag.items[tpagIdx].texturePageId;
    }

    InstanceBBox bbox = Collision_computeBBox(runner, inst);

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timeStr[64];
    if (t != NULL) {
        snprintf(timeStr, sizeof(timeStr), "%04d%02d%02d-%02d%02d%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        snprintf(timeStr, sizeof(timeStr), "20260802-000000");
    }

    const char* roomName = (dw != nullptr && runner->currentRoomIndex >= 0 && (uint32_t)runner->currentRoomIndex < dw->room.count) ?
                            dw->room.rooms[runner->currentRoomIndex].name : "unknown_room";

#if defined(PLATFORM_VITA) || defined(NATIVE_GXM_PROBE_VERSION)
    ensureDirExists("ux0:data/deltarune/deltarunevita/devlogs");
    char dirChapter[256];
    snprintf(dirChapter, sizeof(dirChapter), "ux0:data/deltarune/deltarunevita/devlogs/chapter %d", chapter);
    ensureDirExists(dirChapter);
    char dirObjetos[256];
    snprintf(dirObjetos, sizeof(dirObjetos), "%s/objetos", dirChapter);
    ensureDirExists(dirObjetos);

    char filePath[512];
    snprintf(filePath, sizeof(filePath), "%s/%s_%s.log", dirObjetos, objName, timeStr);
    SceUID fd = sceIoOpen(filePath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        char buf[1024];
        int len = snprintf(buf, sizeof(buf),
            "=== DELTARUNE VITA OBJECT DIAGNOSTIC REPORT ===\n"
            "Timestamp      : %s\n"
            "Chapter        : %d\n"
            "Room           : %s (Index %d)\n"
            "Object Name    : %s (Index %d)\n"
            "Instance ID    : %u\n"
            "Sprite Name    : %s\n"
            "Texture Page   : %d\n"
            "Position (x,y) : (%.2f, %.2f)\n"
            "Depth / Layer  : %.2f / %d\n"
            "Bounding Box   : Left=%d, Top=%d, Right=%d, Bottom=%d (W=%d, H=%d)\n"
            "Precise Mask   : %s\n"
            "===============================================\n",
            timeStr, chapter, roomName, runner->currentRoomIndex,
            objName, inst->objectIndex, inst->instanceId,
            sprName, pageId, (double)inst->x, (double)inst->y,
            (double)inst->depth, inst->layer,
            (int)bbox.left, (int)bbox.top, (int)bbox.right, (int)bbox.bottom,
            (int)(bbox.right - bbox.left), (int)(bbox.bottom - bbox.top),
            (spr && Collision_hasFrameMasks(spr)) ? "YES" : "NO"
        );
        sceIoWrite(fd, buf, len);
        sceIoClose(fd);
        fprintf(stderr, "[OBJ_INSPECT_SAVE] Saved object log to %s\n", filePath);
    }
#else
    char filePath[512];
    snprintf(filePath, sizeof(filePath), "%s_%s.log", objName, timeStr);
    FILE* f = fopen(filePath, "w");
    if (f) {
        fprintf(f,
            "=== DELTARUNE VITA OBJECT DIAGNOSTIC REPORT ===\n"
            "Timestamp      : %s\n"
            "Chapter        : %d\n"
            "Room           : %s (Index %d)\n"
            "Object Name    : %s (Index %d)\n"
            "Instance ID    : %u\n"
            "Sprite Name    : %s\n"
            "Texture Page   : %d\n"
            "Position (x,y) : (%.2f, %.2f)\n"
            "Depth / Layer  : %.2f / %d\n"
            "Bounding Box   : Left=%d, Top=%d, Right=%d, Bottom=%d (W=%d, H=%d)\n"
            "Precise Mask   : %s\n"
            "===============================================\n",
            timeStr, chapter, roomName, runner->currentRoomIndex,
            objName, inst->objectIndex, inst->instanceId,
            sprName, pageId, (double)inst->x, (double)inst->y,
            (double)inst->depth, inst->layer,
            (int)bbox.left, (int)bbox.top, (int)bbox.right, (int)bbox.bottom,
            (int)(bbox.right - bbox.left), (int)(bbox.bottom - bbox.top),
            (spr && Collision_hasFrameMasks(spr)) ? "YES" : "NO"
        );
        fclose(f);
    }
#endif
}

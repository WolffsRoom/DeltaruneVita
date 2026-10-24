#ifndef DELTARUNE_VITA_BACKEND_SELECTION_H
#define DELTARUNE_VITA_BACKEND_SELECTION_H

#include <stdbool.h>

#define VITA_BACKEND_PREFERENCE_PATH \
    "ux0:data/deltarune/deltarunevita/renderer_backend.ini"
#define VITA_BACKEND_PREFERENCE_TEMP_PATH \
    "ux0:data/deltarune/deltarunevita/renderer_backend.tmp"
#define VITA_BACKEND_GXM_BOOT_MARKER \
    "ux0:data/deltarune/deltarunevita/gxm_boot_pending"
#define VITA_BACKEND_VITAGL_EXECUTABLE "app0:/vitagl.bin"
#define VITA_BACKEND_GXM_EXECUTABLE "app0:/gxm.bin"

typedef enum VitaBackend {
    VITA_BACKEND_UNAVAILABLE = -1,
    VITA_BACKEND_VITAGL = 0,
    VITA_BACKEND_GXM = 1
} VitaBackend;

VitaBackend VitaBackendSelection_parse(const char* value);
const char* VitaBackendSelection_name(VitaBackend backend);
const char* VitaBackendSelection_executable(VitaBackend backend);
VitaBackend VitaBackendSelection_resolve(VitaBackend requested,
                                         bool vitaGlAvailable,
                                         bool gxmAvailable);
VitaBackend VitaBackendSelection_load(const char* path);
bool VitaBackendSelection_save(const char* path, VitaBackend backend);

#endif

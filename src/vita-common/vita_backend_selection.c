#include "vita_backend_selection.h"

#include <stdio.h>
#include <string.h>

static bool valueEquals(const char* value, const char* expected) {
    if (value == NULL || expected == NULL) return false;
    size_t expectedLength = strlen(expected);
    if (strncmp(value, expected, expectedLength) != 0) return false;
    value += expectedLength;
    while (*value == '\r' || *value == '\n' || *value == ' ' || *value == '\t')
        ++value;
    return *value == '\0';
}

VitaBackend VitaBackendSelection_parse(const char* value) {
    return valueEquals(value, "gxm") ? VITA_BACKEND_GXM :
                                      VITA_BACKEND_VITAGL;
}

const char* VitaBackendSelection_name(VitaBackend backend) {
    return backend == VITA_BACKEND_GXM ? "gxm" : "vitagl";
}

const char* VitaBackendSelection_executable(VitaBackend backend) {
    return backend == VITA_BACKEND_GXM ? VITA_BACKEND_GXM_EXECUTABLE :
                                         VITA_BACKEND_VITAGL_EXECUTABLE;
}

VitaBackend VitaBackendSelection_resolve(VitaBackend requested,
                                         bool vitaGlAvailable,
                                         bool gxmAvailable) {
    if (requested == VITA_BACKEND_GXM && gxmAvailable)
        return VITA_BACKEND_GXM;
    if (vitaGlAvailable) return VITA_BACKEND_VITAGL;
    return VITA_BACKEND_UNAVAILABLE;
}

VitaBackend VitaBackendSelection_load(const char* path) {
    char value[32] = {0};
    if (path == NULL) return VITA_BACKEND_VITAGL;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return VITA_BACKEND_VITAGL;
    size_t read = fread(value, 1, sizeof(value) - 1U, file);
    fclose(file);
    value[read] = '\0';
    return VitaBackendSelection_parse(value);
}

bool VitaBackendSelection_save(const char* path, VitaBackend backend) {
    if (path == NULL || backend == VITA_BACKEND_UNAVAILABLE) return false;
    char temporary[512];
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written <= 0 || (size_t)written >= sizeof(temporary)) return false;
    FILE* file = fopen(temporary, "wb");
    if (file == NULL) return false;
    const char* value = VitaBackendSelection_name(backend);
    bool ok = fwrite(value, 1, strlen(value), file) == strlen(value) &&
              fwrite("\n", 1, 1, file) == 1 && fclose(file) == 0;
    if (!ok) {
        remove(temporary);
        return false;
    }
    remove(path);
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return false;
    }
    return true;
}

// Draw functions
#ifndef logWarn
#define logWarn(...) fprintf(stderr, __VA_ARGS__)
#endif
#ifdef PLATFORM_VITA
#define vertexTrace(...) ((void)0)
#else
#define vertexTrace(...) printf(__VA_ARGS__)
#endif
#ifndef logError
#define logError(...) fprintf(stderr, __VA_ARGS__)
#endif
static RValue builtin_draw_primitive_begin(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (1 > argCount) {
        logWarn("[draw_primitive_begin]: Expected 1 argument, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t primitiveType = RValue_toInt32(args[0]);
    Renderer_primitiveBegin(runner->renderer, primitiveType);
    return RValue_makeUndefined();
}

static RValue builtin_draw_primitive_begin_texture(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) {
        logWarn("[draw_primitive_begin_texture]: Expected 2 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }
    
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    int32_t primitiveType = RValue_toInt32(args[0]);
    int32_t textureId = RValue_toInt32(args[1]);
    Renderer_primitiveBeginTexture(runner->renderer, primitiveType, textureId);
    return RValue_makeUndefined();
}

static RValue builtin_draw_primitive_end(MAYBE_UNUSED VMContext* ctx, MAYBE_UNUSED RValue* args, MAYBE_UNUSED int32_t argCount) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    Renderer_primitiveEnd(runner->renderer);
    return RValue_makeUndefined();
}

static RValue builtin_draw_vertex(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (2 > argCount) {
        logWarn("[draw_vertex]: Expected 2 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    float z = x;
    uint32_t color = runner->renderer->drawColor;
    float alpha = 1.0f;
    float u = 0.0f;
    float v = 0.0f;

    Renderer_drawVertex(runner->renderer, x, y, z, color, alpha, u, v);
    return RValue_makeUndefined();
}

static RValue builtin_draw_vertex_color(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (4 > argCount) {
        logWarn("[draw_vertex_color]: Expected 4 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    float z = 0.0f;
    uint32_t color = (uint32_t) RValue_toInt32(args[2]);
    float alpha = (float) RValue_toReal(args[3]);
    float u = 0.0f;
    float v = 0.0f;

    if (alpha > 1.0f) alpha = 1.0f;
    if (alpha < 0.0f) alpha = 0.0f;

    Renderer_drawVertex(runner->renderer, x, y, z, color, alpha, u, v);
    return RValue_makeUndefined();
}

static RValue builtin_draw_vertex_texture(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (4 > argCount) {
        logWarn("[draw_vertex_texture]: Expected 4 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    float z = 0.0f;
    uint32_t color = runner->renderer->drawColor;
    float alpha = 1.0f;
    float u = (float) RValue_toReal(args[2]);
    float v = (float) RValue_toReal(args[3]);

    Renderer_drawVertex(runner->renderer, x, y, z, color, alpha, u, v);
    return RValue_makeUndefined();
}

static RValue builtin_draw_vertex_texture_color(MAYBE_UNUSED VMContext* ctx, RValue* args, MAYBE_UNUSED int32_t argCount) {
    if (6 > argCount) {
        logWarn("[draw_vertex_texture_color]: Expected 6 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return RValue_makeUndefined();

    float x = (float) RValue_toReal(args[0]);
    float y = (float) RValue_toReal(args[1]);
    float z = 0.0f;
    float u = (float) RValue_toReal(args[2]);
    float v = (float) RValue_toReal(args[3]);
    uint32_t color = (uint32_t) RValue_toInt32(args[4]);
    float alpha = (float) RValue_toReal(args[5]);

    if (alpha > 1.0f) alpha = 1.0f;
    if (alpha < 0.0f) alpha = 0.0f;

    Renderer_drawVertex(runner->renderer, x, y, z, color, alpha, u, v);
    return RValue_makeUndefined();
}


// Vertex Formats

enum yyVertexType {
    yyVTFLOAT1 = 0x1,
    yyVTFLOAT2 = 0x2,
    yyVTFLOAT3 = 0x3,
    yyVTFLOAT4 = 0x4,
    yyVTCOLOR = 0x5,
    yyVTUBYTE4 = 0x6,
    yyVTMaxType = 0x6
};

enum yyVertexUsage {
    yyVUPOSITION = 0x1,
    yyVUCOLOR = 0x2,
    yyVUNORMAL = 0x3,
    yyVUTEXCOORD = 0x4,
    yyVUBLENDWEIGHT = 0x5,
    yyVUBLENDINDICES = 0x6,
    yyVUPSIZE = 0x7,
    yyVUTANGENT = 0x8,
    yyVUBINORMAL = 0x9,
    yyVUTESSFACTOR = 0xa,
    yyVUPOSITIONT = 0xb,
    yyVUFOG = 0xc,
    yyVUDEPTH = 0xd,
    yyVUSAMPLER = 0xe,
    yyVUMaxVertexUsage = 0xe
} yyVertexUsage;

typedef struct {
    int32_t offset;
    enum yyVertexType type;
    enum yyVertexUsage usage;
    uint32_t bit;
} VmVertexElement;

typedef struct {
    uint32_t id;
    uint32_t count;
    VmVertexElement *format;
    void *pNative;
    uint32_t bitMask;
    uint32_t size;
    uint32_t yyFVF;
} VmVertexFormat;

#define RESOURCE_VERTEX_FORMAT 0x1000000

static VmVertexFormat** g_VertexFormats = nullptr;
VmVertexFormat* g_NewFormat = nullptr;
uint32_t g_FormatBit = 0;
int g_currVertexFormatID = 1;

static inline uint32_t vertexFormatPendingResourceId(void) {
    return ((uint32_t) g_currVertexFormatID) | RESOURCE_VERTEX_FORMAT;
}

static inline uint32_t vertexFormatIdToIndex(uint32_t formatId) {
    return formatId & ~RESOURCE_VERTEX_FORMAT;
}

static VmVertexFormat* vertexFormatById(uint32_t formatId) {
    uint32_t index = vertexFormatIdToIndex(formatId);
    if (index == 0 || index >= (uint32_t) arrlen(g_VertexFormats)) return nullptr;
    return g_VertexFormats[index];
}

static void registerVertexFormat(VmVertexFormat* vertexFormat) {
    uint32_t index = vertexFormatIdToIndex(vertexFormat->id);
    if (index == 0) return;
    if (index >= (uint32_t) arrlen(g_VertexFormats)) {
        arrsetlen(g_VertexFormats, (int32_t)(index + 1));
    }
    g_VertexFormats[index] = vertexFormat;
}

static RValue builtin_vertex_format_begin(VMContext* ctx, MAYBE_UNUSED RValue* args, int32_t argCount) {
    if (argCount != 0) {
        logWarn("[vertex_format_begin] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat != nullptr) {
        logWarn("[vertex_format_begin] Can't start a new format, still creating another one.\n");
        return RValue_makeUndefined();
    }

    VmVertexFormat* vertexFormat = (VmVertexFormat*) safeMalloc(sizeof(VmVertexFormat));

    if (vertexFormat == nullptr) {
        logError("[vertex_format_begin] Memory allocation failed\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_begin] Begin format build: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    vertexFormat->id = 0;
    vertexFormat->count = 0;
    vertexFormat->format = nullptr;
    vertexFormat->pNative = nullptr;
    vertexFormat->bitMask = 0;
    vertexFormat->size = 0;
    vertexFormat->yyFVF = 0;

    g_NewFormat = vertexFormat;
    g_FormatBit = 1;

    return RValue_makeUndefined();
}



static int vertexFormatTypeSize(enum yyVertexType type) {
    switch (type) {
    case yyVTFLOAT1:
        return 4;
    case yyVTFLOAT2:
        return 8;
    case yyVTFLOAT3:
        return 12;
    case yyVTFLOAT4:
        return 16;
    case yyVTCOLOR:
    case yyVTUBYTE4:
        return 4;
    default:
        return 0;
    }
}

static bool vertexFormatAddElement(VmVertexFormat* vertexFormat, enum yyVertexType type, enum yyVertexUsage usage) {
    if (vertexFormat == nullptr || g_FormatBit == 0) {
        return false;
    }

    arrsetlen(vertexFormat->format, (int32_t)(vertexFormat->count + 1));

    VmVertexElement* element = &vertexFormat->format[vertexFormat->count];
    element->offset = vertexFormat->size;
    element->type = type;
    element->usage = usage;
    element->bit = g_FormatBit;

    vertexFormat->count++;
    vertexFormat->bitMask |= g_FormatBit;
    vertexFormat->size += vertexFormatTypeSize(type);
    g_FormatBit <<= 1;
    return true;
}

static RValue builtin_vertex_format_add_color(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    if (argCount != 0) {
        logWarn("[vertex_format_add_colour] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_colour] Can't add colour, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_colour] Too many elements\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_colour] Adding color element to pending format: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, yyVTCOLOR, yyVUCOLOR)) {
        logWarn("[vertex_format_add_colour] Failed to add colour element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_add_position(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    if (argCount != 0) {
        logWarn("[vertex_format_add_position] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_position] Can't add position, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_position] Too many elements\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_position] Adding position element to pending format: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, yyVTFLOAT2, yyVUPOSITION)) {
        logWarn("[vertex_format_add_position] Failed to add position element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_add_position_3d(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    if (argCount != 0) {
        logWarn("[vertex_format_add_position_3d] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_position_3d] Can't add 3D position, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_position_3d] Too many elements\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_position_3d] Adding 3D position element to pending format: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, yyVTFLOAT3, yyVUPOSITION)) {
        logWarn("[vertex_format_add_position_3d] Failed to add position element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_add_textcoord(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    if (argCount != 0) {
        logWarn("[vertex_format_add_textcoord] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_textcoord] Can't add textcoord, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_textcoord] Too many elements\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_textcoord] Adding texture coordinate element to pending format: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, yyVTFLOAT2, yyVUTEXCOORD)) {
        logWarn("[vertex_format_add_textcoord] Failed to add texture coordinate element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_add_normal(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    if (argCount != 0) {
        logWarn("[vertex_format_add_normal] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_normal] Can't add normal, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_normal] Too many elements\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_normal] Adding normal element to pending format: slot=%d, pending_resource_id=%u\n",
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, yyVTFLOAT3, yyVUNORMAL)) {
        logWarn("[vertex_format_add_normal] Failed to add normal element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_add_custom(
    MAYBE_UNUSED VMContext* ctx,
    RValue* args,
    int32_t argCount
) {
    if (argCount != 2) {
        logWarn("[vertex_format_add_custom] Expected 2 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (g_NewFormat == nullptr) {
        logWarn("[vertex_format_add_custom] Can't add custom element, no format is under construction.\n");
        return RValue_makeUndefined();
    }

    if (g_FormatBit == 0) {
        logWarn("[vertex_format_add_custom] Too many elements\n");
        return RValue_makeUndefined();
    }

    enum yyVertexType type = (enum yyVertexType) RValue_toInt32(args[0]);
    if (type < yyVTFLOAT1 || type > yyVTMaxType) {
        logWarn("[vertex_format_add_custom] Illegal types\n");
        return RValue_makeUndefined();
    }

    enum yyVertexUsage usage = (enum yyVertexUsage) RValue_toInt32(args[1]);
    if (usage < yyVUPOSITION || usage > yyVUMaxVertexUsage) {
        logWarn("[vertex_format_add_custom] Illegal usage\n");
        return RValue_makeUndefined();
    }

        vertexTrace("[vertex_format_add_custom] Adding custom element: type=%d, usage=%d to pending format: slot=%d, pending_resource_id=%u\n",
            type,
            usage,
            g_currVertexFormatID,
            vertexFormatPendingResourceId());

    if (!vertexFormatAddElement(g_NewFormat, type, usage)) {
        logWarn("[vertex_format_add_custom] Failed to add custom element\n");
        return RValue_makeUndefined();
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_end(
    MAYBE_UNUSED VMContext* ctx,
    MAYBE_UNUSED RValue* args,
    int32_t argCount
) {
    VmVertexFormat* vertexFormat = g_NewFormat;

    if (argCount != 0) {
        logWarn("[vertex_format_end] Expected 0 arguments, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    if (vertexFormat == nullptr) {
        logWarn(
            "[vertex_format_end] Can't end format, you haven't started one yet.\n"
        );
        return RValue_makeUndefined();
    }

    uint32_t formatId = g_currVertexFormatID | RESOURCE_VERTEX_FORMAT;
    vertexFormat->id = formatId;
    g_currVertexFormatID++;
    registerVertexFormat(vertexFormat);

    void* nativeFormat = nullptr;
    int usageCounts[16] = {0};

    if (vertexFormat->count > 0 && vertexFormat->format != nullptr) {
        typedef struct {
            uint32_t offset;
            uint32_t usageIndex;
            uint32_t type;
            uint32_t method;
            uint32_t usage;
            uint32_t normalized;
        } VertexElementNative;

        nativeFormat = malloc(sizeof(VertexElementNative) * vertexFormat->count);
        if (nativeFormat == nullptr) {
            logError("[vertex_format_end] Memory allocation failed\n");
            return RValue_makeUndefined();
        }

        VertexElementNative* nativeElements = (VertexElementNative*) nativeFormat;

        for (uint32_t i = 0; i < vertexFormat->count; i++) {
            VmVertexElement* element = &vertexFormat->format[i];
            uint32_t nativeType = 0x02;

            switch (element->type) {
                case yyVTFLOAT1:
                    nativeType = 0x29;
                    break;
                case yyVTFLOAT2:
                    nativeType = 0x10;
                    break;
                case yyVTFLOAT3:
                    nativeType = 0x06;
                    break;
                case yyVTFLOAT4:
                    nativeType = 0x1c;
                    break;
                case yyVTCOLOR:
                    nativeType = 0x1c;
                    break;
                case yyVTUBYTE4:
                    nativeType = 0x1e;
                    break;
                default:
                    break;
            }

            nativeElements[i].offset = element->offset;
            nativeElements[i].usageIndex = usageCounts[element->usage];
            nativeElements[i].type = nativeType;
            nativeElements[i].method = 0;
            nativeElements[i].usage = element->usage;
            nativeElements[i].normalized = 0;

            if (element->usage < yyVUMaxVertexUsage) {
                usageCounts[element->usage]++;
            }
        }
    }

        vertexTrace("[vertex_format_end] Finalized vertex format: slot=%u, resource_id=%u, elements=%u, stride=%u bytes\n",
            vertexFormatIdToIndex(formatId),
            formatId,
            vertexFormat->count,
            vertexFormat->size);

    vertexFormat->pNative = nativeFormat;
    g_NewFormat = nullptr;
    g_FormatBit = 0;

    return RValue_makeInt32((int32_t) formatId);
}


static RValue builtin_vertex_format_delete(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_format_delete] Expected 1 argument, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    uint32_t formatId = (uint32_t) RValue_toInt32(args[0]);
    if ((formatId & RESOURCE_VERTEX_FORMAT) != RESOURCE_VERTEX_FORMAT) {
        return RValue_makeUndefined();
    }

    if (ctx != nullptr && ctx->runner != nullptr && ctx->runner->renderer != nullptr && ctx->runner->renderer->vtable != nullptr && ctx->runner->renderer->vtable->flush != nullptr) {
        ctx->runner->renderer->vtable->flush(ctx->runner->renderer);
    }

    VmVertexFormat* vertexFormat = vertexFormatById(formatId);
    if (vertexFormat == nullptr) {
        return RValue_makeUndefined();
    }

    if (vertexFormat->pNative != nullptr) {
        free(vertexFormat->pNative);
        vertexFormat->pNative = nullptr;
    }

    if (vertexFormat->format != nullptr) {
        free(vertexFormat->format);
        vertexFormat->format = nullptr;
    }

    uint32_t index = vertexFormatIdToIndex(formatId);
    if (index < (uint32_t) arrlen(g_VertexFormats)) {
        g_VertexFormats[index] = nullptr;
    }

    free(vertexFormat);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_format_exists(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_format_exists] Expected 1 argument, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    uint32_t formatId = (uint32_t) RValue_toInt32(args[0]);
    if ((formatId & RESOURCE_VERTEX_FORMAT) != RESOURCE_VERTEX_FORMAT) {
        return RValue_makeBool(false);
    }

    VmVertexFormat* vertexFormat = vertexFormatById(formatId);
    return RValue_makeBool(vertexFormat != nullptr);
}

static RValue builtin_vertex_format_get_info(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_format_get_info] Expected 1 argument, got %d\n", argCount);
        return RValue_makeUndefined();
    }

    uint32_t formatId = (uint32_t) RValue_toInt32(args[0]);
    if ((formatId & RESOURCE_VERTEX_FORMAT) != RESOURCE_VERTEX_FORMAT) {
        return RValue_makeUndefined();
    }

    VmVertexFormat* vertexFormat = vertexFormatById(formatId);
    if (vertexFormat == nullptr) {
        return RValue_makeUndefined();
    }

    Instance* ret = Runner_createStruct(ctx->runner);
    VM_structSetAndFreeVal(ctx, ret, "stride", RValue_makeInt32((int32_t) vertexFormat->size), 0);
    VM_structSetAndFreeVal(ctx, ret, "num_elements", RValue_makeInt32((int32_t) vertexFormat->count), 1);

    GMLArray* elements = GMLArray_create(ctx->dataWin->gen8.wadVersion,
        vertexFormat->count > 0 ? (int32_t) vertexFormat->count : 0);

    for (uint32_t i = 0; i < vertexFormat->count; i++) {
        Instance* element = Runner_createStruct(ctx->runner);
        VmVertexElement* current = &vertexFormat->format[i];

        VM_structSetAndFreeVal(ctx, element, "usage", RValue_makeInt32((int32_t) current->usage), -1);
        VM_structSetAndFreeVal(ctx, element, "type", RValue_makeInt32((int32_t) current->type), -1);
        VM_structSetAndFreeVal(ctx, element, "size", RValue_makeInt32(vertexFormatTypeSize(current->type)), -1);
        VM_structSetAndFreeVal(ctx, element, "offset", RValue_makeInt32(current->offset), -1);

        *GMLArray_slot(elements, (int32_t) i) = RValue_makeStructAndIncRef(element);
    }

    VM_structSetAndFreeVal(ctx, ret, "elements", RValue_makeArray(elements), -1);
    return RValue_makeStructAndIncRef(ret);
}

// Vertex Buffers

typedef struct VmVertexBuffer {
    struct VmVertexBuffer* pPrev;
    struct VmVertexBuffer* pNext;
    uint32_t flags;
    void *pVertexBuffer;
    void *pHW0;
    void *pHW1;
    void *pHW2;
    void *pHW3;
    int32_t FVF;
    int32_t FVFSize;
    int32_t lockCurrent;
    int32_t current;
    int32_t total;
    void *pAddress;
    uint64_t frameLock;
} VmVertexBuffer;

typedef union Buffer_Vertex_u_0 {
    uint32_t *pBuffer32;
    uint16_t *pBuffer16;
    uint8_t  *pBuffer8;
    float    *pBufferF32;
} Buffer_Vertex_u_0;

typedef struct {
    Buffer_Vertex_u_0 buffer;
    uint32_t bufferSize;
    uint32_t streamStart;
    uint32_t streamCurrent;
    uint32_t elementIndex;
    uint32_t elementCount;
    uint32_t currentMask;
    int32_t vertexCount;
    uint32_t deletionCountdown;
    bool frozen;
    int32_t currentFormat;
    int32_t storedFormat;
    VmVertexFormat *pCurrentFormatVFRelease;
    VmVertexBuffer *pFrozenVB;
} Buffer_Vertex;

static Buffer_Vertex** g_VertexBuffers = nullptr;
static int32_t g_VertexBufferCount = 0;

#ifdef PLATFORM_VITA
// DELTARUNE Chapter 3 perspective helpers create, submit and delete a tiny
// vertex buffer for every projected sprite. Recycle one CPU-side builder so
// those hundreds of four-vertex quads do not malloc/free 32 KiB apiece.
// The resource ID is still removed from g_VertexBuffers on delete, preserving
// GameMaker's vertex_buffer_exists() semantics.
static Buffer_Vertex* g_VitaTransientVertexBufferCache = nullptr;

static Buffer_Vertex* vitaTakeTransientVertexBuffer(int32_t bufferSize) {
    Buffer_Vertex* buffer = g_VitaTransientVertexBufferCache;
    if (buffer == nullptr) return nullptr;
    g_VitaTransientVertexBufferCache = nullptr;

    if ((int32_t)buffer->bufferSize < bufferSize) {
        uint8_t* grown = (uint8_t*)safeRealloc(buffer->buffer.pBuffer8, (size_t)bufferSize);
        if (grown == nullptr) {
            free(buffer->buffer.pBuffer8);
            free(buffer);
            return nullptr;
        }
        buffer->buffer.pBuffer8 = grown;
        buffer->bufferSize = (uint32_t)bufferSize;
    }

    uint8_t* data = buffer->buffer.pBuffer8;
    uint32_t capacity = buffer->bufferSize;
    memset(buffer, 0, sizeof(*buffer));
    buffer->buffer.pBuffer8 = data;
    buffer->bufferSize = capacity;
    buffer->currentFormat = -1;
    buffer->storedFormat = -1;
    return buffer;
}

static void vitaCacheTransientVertexBuffer(Buffer_Vertex* buffer) {
    if (buffer == nullptr) return;
    if (g_VitaTransientVertexBufferCache != nullptr) {
        free(g_VitaTransientVertexBufferCache->buffer.pBuffer8);
        free(g_VitaTransientVertexBufferCache);
    }
    g_VitaTransientVertexBufferCache = buffer;
}
#endif

static int allocBufferVertex(int32_t bufferSize) {
    if (bufferSize < 0) {
        bufferSize = 0;
    }

    if (g_VertexBuffers != nullptr && g_VertexBufferCount > 0) {
        for (int32_t i = 0; i < g_VertexBufferCount; ++i) {
            if (g_VertexBuffers[i] == nullptr) {
                Buffer_Vertex* buffer = nullptr;
#ifdef PLATFORM_VITA
                buffer = vitaTakeTransientVertexBuffer(bufferSize);
#endif
                if (buffer == nullptr) {
                    buffer = (Buffer_Vertex*) safeMalloc(sizeof(Buffer_Vertex));
                    if (buffer == nullptr) {
                        logError("[vertex_create_buffer] Memory allocation failed\n");
                        return -1;
                    }

                    memset(buffer, 0, sizeof(*buffer));
                    if (bufferSize > 0) {
                        buffer->buffer.pBuffer8 = (uint8_t*) safeMalloc((size_t) bufferSize);
                        if (buffer->buffer.pBuffer8 == nullptr) {
                            free(buffer);
                            logError("[vertex_create_buffer] Memory allocation failed\n");
                            return -1;
                        }
                    }
                    buffer->bufferSize = (uint32_t) bufferSize;
                    buffer->currentFormat = -1;
                    buffer->storedFormat = -1;
                }

                g_VertexBuffers[i] = buffer;
                return i;
            }
        }
    }

    int32_t oldCount = g_VertexBufferCount;
    int32_t newCount = oldCount == 0 ? 32 : oldCount * 2;

    Buffer_Vertex** newBuffers = (Buffer_Vertex**) safeRealloc(
        g_VertexBuffers,
        (size_t) newCount * sizeof(Buffer_Vertex*)
    );
    if (newBuffers == nullptr) {
        logError("[vertex_create_buffer] Memory allocation failed\n");
        return -1;
    }

    if (newCount > oldCount) {
        memset(newBuffers + oldCount, 0, (size_t)(newCount - oldCount) * sizeof(Buffer_Vertex*));
    }

    g_VertexBuffers = newBuffers;
    g_VertexBufferCount = newCount;

    Buffer_Vertex* buffer = (Buffer_Vertex*) safeMalloc(sizeof(Buffer_Vertex));
    if (buffer == nullptr) {
        logError("[vertex_create_buffer] Memory allocation failed\n");
        return -1;
    }

    memset(buffer, 0, sizeof(*buffer));
    if (bufferSize > 0) {
        buffer->buffer.pBuffer8 = (uint8_t*) safeMalloc((size_t) bufferSize);
        if (buffer->buffer.pBuffer8 == nullptr) {
            free(buffer);
            logError("[vertex_create_buffer] Memory allocation failed\n");
            return -1;
        }
    }

    buffer->bufferSize = (uint32_t) bufferSize;
    buffer->currentFormat = -1;
    buffer->storedFormat = -1;

    g_VertexBuffers[oldCount] = buffer;
    return oldCount;
}

static RValue builtin_vertex_create_buffer_ext(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_create_buffer_ext] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferSize = RValue_toInt32(args[0]);
    if (bufferSize < 0x100) {
        bufferSize = 0x100;
    }

    int32_t bufferIndex = allocBufferVertex(bufferSize);
    return RValue_makeInt32(bufferIndex);
}

static RValue builtin_vertex_create_buffer(VMContext* ctx, MAYBE_UNUSED RValue* args, int32_t argCount) {
    if (argCount != 0) {
        logWarn("[vertex_create_buffer] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = allocBufferVertex(0x8000);
    return RValue_makeInt32(bufferIndex);
}

static RValue builtin_vertex_create_buffer_from_buffer_ext(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 4) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferId = RValue_toInt32(args[0]);
    uint32_t formatId = (uint32_t) RValue_toInt32(args[1]);
    int32_t srcOffset = RValue_toInt32(args[2]);
    int32_t vertNum = RValue_toInt32(args[3]);

    if (srcOffset < 0) srcOffset = 0;
    if (vertNum < -1) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Illegal vertex count: %d", vertNum);
        return RValue_makeInt32(-1);
    }

    GmlBuffer* srcBuffer = gmlBufferGet(ctx->runner, bufferId);
    if (srcBuffer == nullptr) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Source buffer does not exist");
        return RValue_makeInt32(-1);
    }

    VmVertexFormat* vertexFormat = vertexFormatById(formatId);
    if (vertexFormat == nullptr) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Specified vertex format doesn't exist");
        return RValue_makeInt32(-1);
    }

    int32_t vertexSize = (int32_t) vertexFormat->size;
    if (vertexSize <= 0) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Invalid vertex format stride");
        return RValue_makeInt32(-1);
    }

    if (srcOffset > srcBuffer->usedSize) {
        logWarn("[vertex_create_buffer_from_buffer_ext] Source offset is beyond buffer size");
        return RValue_makeInt32(-1);
    }

    int32_t totalBytes;
    if (vertNum == -1) {
        totalBytes = srcBuffer->usedSize - srcOffset;
        if (totalBytes < 0) {
            totalBytes = 0;
        }
        vertNum = totalBytes / vertexSize;
        totalBytes = vertNum * vertexSize;
    } else {
        int64_t requestedBytes = (int64_t) vertNum * vertexSize;
        if (requestedBytes + srcOffset > srcBuffer->usedSize) {
            totalBytes = srcBuffer->usedSize - srcOffset;
            if (totalBytes < 0) {
                totalBytes = 0;
            }
            vertNum = totalBytes / vertexSize;
            totalBytes = vertNum * vertexSize;
        } else {
            totalBytes = (int32_t) requestedBytes;
        }
    }

    int32_t bufferIndex = allocBufferVertex(totalBytes);
    if (bufferIndex < 0) {
        return RValue_makeInt32(-1);
    }

    Buffer_Vertex* dest = g_VertexBuffers[bufferIndex];
    if (dest == nullptr) {
        return RValue_makeInt32(-1);
    }

    if (totalBytes > 0 && dest->buffer.pBuffer8 != nullptr) {
        memcpy(dest->buffer.pBuffer8, srcBuffer->data + srcOffset, (size_t) totalBytes);
    }

    dest->bufferSize = (uint32_t) totalBytes;
    dest->vertexCount = vertNum;
    dest->currentFormat = (int32_t) formatId;
    dest->storedFormat = (int32_t) formatId;
    dest->streamStart = 0;
    dest->elementIndex = 0;
    dest->elementCount = 0;
    dest->currentMask = 0;
    dest->deletionCountdown = 0;
    dest->frozen = false;
    dest->pCurrentFormatVFRelease = nullptr;
    dest->pFrozenVB = nullptr;

    return RValue_makeInt32(bufferIndex);
}

static RValue builtin_vertex_create_buffer_from_buffer(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 2) {
        logWarn("[vertex_create_buffer_from_buffer] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    RValue extArgs[4];
    extArgs[0] = args[0];
    extArgs[1] = args[1];
    extArgs[2] = RValue_makeInt32(0);
    extArgs[3] = RValue_makeInt32(-1);
    return builtin_vertex_create_buffer_from_buffer_ext(ctx, extArgs, 4);
}

static int vertexFormatElementSize(enum yyVertexType type) {
    switch (type) {
        case yyVTFLOAT1:
        case yyVTFLOAT2:
        case yyVTFLOAT3:
        case yyVTFLOAT4:
            return 4;
        case yyVTCOLOR:
        case yyVTUBYTE4:
            return 1;
        default:
            return 0;
    }
}

static bool vertexBufferOffsetIsAligned(Buffer_Vertex* buffer, uint32_t offset) {
    if (buffer == nullptr) return false;

    VmVertexFormat* vertexFormat = vertexFormatById((uint32_t) buffer->storedFormat);
    if (vertexFormat == nullptr || vertexFormat->count == 0 || vertexFormat->format == nullptr) {
        return false;
    }

    uint32_t vertexSize = vertexFormat->size;
    if (vertexSize == 0) return false;

    uint32_t vertexIndex = offset / vertexSize;
    uint32_t offsetInVertex = offset - vertexIndex * vertexSize;

    for (uint32_t i = 0; i < vertexFormat->count; ++i) {
        VmVertexElement* element = &vertexFormat->format[i];
        uint32_t elementTypeSize = (uint32_t) vertexFormatTypeSize(element->type);

        if (offsetInVertex >= (uint32_t)element->offset && offsetInVertex < element->offset + elementTypeSize) {
            int elementSize = vertexFormatElementSize(element->type);
            if (elementSize <= 0) return false;

            uint32_t elementOffset = offsetInVertex - element->offset;
            return (elementOffset % (uint32_t) elementSize) == 0;
        }
    }

    return false;
}

static RValue builtin_vertex_update_buffer_from_buffer(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount < 3 || argCount > 5) {
        logWarn("[vertex_update_buffer_from_buffer] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferId = RValue_toInt32(args[0]);
    int32_t destOffset = RValue_toInt32(args[1]);
    int32_t srcBufferId = RValue_toInt32(args[2]);
    int32_t srcOffset = 0;
    int32_t srcSize = -1;

    if (argCount >= 4) {
        srcOffset = RValue_toInt32(args[3]);
    }
    if (argCount >= 5) {
        srcSize = RValue_toInt32(args[4]);
    }

    if (destOffset < 0) {
        logWarn("[vertex_update_buffer_from_buffer] destination offset must be a positive number");
        return RValue_makeUndefined();
    }

    if (bufferId < 0 || bufferId >= g_VertexBufferCount) {
        logWarn("[vertex_update_buffer_from_buffer] Vertex Buffer index is out of range");
        return RValue_makeUndefined();
    }

    Buffer_Vertex* dest = g_VertexBuffers[bufferId];
    if (dest == nullptr) {
        logWarn("[vertex_update_buffer_from_buffer] Vertex Buffer index is out of range");
        return RValue_makeUndefined();
    }

    if (srcOffset < 0) {
        logWarn("[vertex_update_buffer_from_buffer] source offset must be a positive number");
        return RValue_makeUndefined();
    }

    GmlBuffer* srcBuffer = gmlBufferGet(ctx->runner, srcBufferId);
    if (srcBuffer == nullptr) {
        logWarn("[vertex_update_buffer_from_buffer] specified buffer doesn't exist");
        return RValue_makeUndefined();
    }

    if (srcOffset > srcBuffer->usedSize) {
        logWarn("[vertex_update_buffer_from_buffer] source offset is beyond buffer size");
        return RValue_makeUndefined();
    }

    if (srcSize < 0) {
        srcSize = srcBuffer->usedSize;
    }

    if ((int64_t) srcSize + srcOffset > srcBuffer->usedSize) {
        srcSize = srcBuffer->usedSize - srcOffset;
    }

    if (srcSize == 0) {
        return RValue_makeUndefined();
    }

    if (dest->frozen) {
        logWarn("[vertex_update_buffer_from_buffer] cannot update a frozen vertex buffer");
        return RValue_makeUndefined();
    }

    VmVertexFormat* vertexFormat = vertexFormatById((uint32_t) dest->storedFormat);
    if (vertexFormat == nullptr) {
        logWarn("[vertex_update_buffer_from_buffer] unknown vertex buffer format");
        return RValue_makeUndefined();
    }

    int32_t vertexSize = (int32_t) vertexFormat->size;
    if (vertexSize <= 0) {
        logWarn("[vertex_update_buffer_from_buffer] invalid vertex buffer format stride");
        return RValue_makeUndefined();
    }

    if (!vertexBufferOffsetIsAligned(dest, (uint32_t) destOffset)) {
        logWarn("[vertex_update_buffer_from_buffer] destination offset must be aligned to a vertex element");
        return RValue_makeUndefined();
    }

    int64_t destSize64 = (int64_t) destOffset + (int64_t) srcSize;
    if (destSize64 < 0 || destSize64 > INT32_MAX) {
        logWarn("[vertex_update_buffer_from_buffer] destination size is invalid");
        return RValue_makeUndefined();
    }

    uint32_t destSize = (uint32_t) destSize64;
    if (!vertexBufferOffsetIsAligned(dest, destSize)) {
        logWarn("[vertex_update_buffer_from_buffer] destination size must be aligned to a vertex element");
        return RValue_makeUndefined();
    }

    if (destSize > dest->bufferSize) {
        uint8_t* newBuffer = (uint8_t*) safeRealloc(dest->buffer.pBuffer8, (size_t) destSize);
        if (newBuffer == nullptr) {
            logError("[vertex_update_buffer_from_buffer] Memory allocation failed\n");
            return RValue_makeUndefined();
        }

        dest->buffer.pBuffer8 = newBuffer;
        dest->bufferSize = destSize;
    }

    if (srcSize > 0 && dest->buffer.pBuffer8 != nullptr) {
        memcpy(dest->buffer.pBuffer8 + destOffset, srcBuffer->data + srcOffset, (size_t) srcSize);
    }

    if ((uint32_t) dest->vertexCount < (uint32_t) (destSize / vertexSize)) {
        dest->vertexCount = (int32_t) (destSize / vertexSize);
    }

    dest->currentFormat = -1;
    dest->streamStart = 0;
    dest->elementIndex = 0;
    dest->elementCount = vertexFormat->count;
    dest->currentMask = 0;
    dest->pCurrentFormatVFRelease = nullptr;

    return RValue_makeUndefined();
}

static RValue builtin_vertex_update_buffer_from_vertex(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount < 3 || argCount > 5) {
        logWarn("[vertex_update_buffer_from_vertex] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t destBufferId = RValue_toInt32(args[0]);
    int32_t destVertex = RValue_toInt32(args[1]);
    int32_t srcBufferId = RValue_toInt32(args[2]);
    int32_t srcVertex = 0;
    int32_t srcVertNum = -1;

    if (argCount >= 4) {
        srcVertex = RValue_toInt32(args[3]);
    }
    if (argCount >= 5) {
        srcVertNum = RValue_toInt32(args[4]);
    }

    if (destVertex < 0) {
        logWarn("[vertex_update_buffer_from_vertex] destination vertex must be a positive number");
        return RValue_makeUndefined();
    }

    if (srcVertex < 0) {
        logWarn("[vertex_update_buffer_from_vertex] source vertex must be a positive number");
        return RValue_makeUndefined();
    }

    if (destBufferId < 0 || destBufferId >= g_VertexBufferCount) {
        logWarn("[vertex_update_buffer_from_vertex] destination vertex buffer index is out of range");
        return RValue_makeUndefined();
    }

    Buffer_Vertex* dest = g_VertexBuffers[destBufferId];
    if (dest == nullptr) {
        logWarn("[vertex_update_buffer_from_vertex] destination vertex buffer index is out of range");
        return RValue_makeUndefined();
    }

    if (srcBufferId < 0 || srcBufferId >= g_VertexBufferCount) {
        logWarn("[vertex_update_buffer_from_vertex] source vertex buffer index is out of range");
        return RValue_makeUndefined();
    }

    Buffer_Vertex* src = g_VertexBuffers[srcBufferId];
    if (src == nullptr) {
        logWarn("[vertex_update_buffer_from_vertex] source vertex buffer index is out of range");
        return RValue_makeUndefined();
    }

    if (dest == src) {
        logWarn("[vertex_update_buffer_from_vertex] source and destination cannot be the same vertex buffer");
        return RValue_makeUndefined();
    }

    if (dest->frozen) {
        logWarn("[vertex_update_buffer_from_vertex] destination vertex buffer cannot be frozen");
        return RValue_makeUndefined();
    }

    if (src->frozen) {
        logWarn("[vertex_update_buffer_from_vertex] source vertex buffer cannot be frozen");
        return RValue_makeUndefined();
    }

    VmVertexFormat* srcFormat = vertexFormatById((uint32_t) src->storedFormat);
    if (srcFormat == nullptr) {
        logWarn("[vertex_update_buffer_from_vertex] unknown source vertex buffer format");
        return RValue_makeUndefined();
    }

    VmVertexFormat* destFormat = vertexFormatById((uint32_t) dest->storedFormat);
    if (destFormat == nullptr) {
        dest->storedFormat = src->storedFormat;
        destFormat = srcFormat;
    } else {
        if (destFormat->count != srcFormat->count) {
            logWarn("[vertex_update_buffer_from_vertex] source and destination vertex buffers must use the same vertex format");
            return RValue_makeUndefined();
        }

        for (uint32_t i = 0; i < destFormat->count; ++i) {
            VmVertexElement* destElem = &destFormat->format[i];
            VmVertexElement* srcElem = &srcFormat->format[i];
            if (destElem->offset != srcElem->offset || destElem->type != srcElem->type ||
                destElem->usage != srcElem->usage || destElem->bit != srcElem->bit) {
                logWarn("[vertex_update_buffer_from_vertex] source and destination vertex buffers must use the same vertex format");
                return RValue_makeUndefined();
            }
        }
    }

    int32_t vertexSize = (int32_t) destFormat->size;
    if (vertexSize <= 0) {
        logWarn("[vertex_update_buffer_from_vertex] invalid vertex buffer format");
        return RValue_makeUndefined();
    }

    int32_t srcOffset = srcVertex * vertexSize;
    int32_t maxSrcBytes = src->vertexCount * vertexSize;
    if (srcOffset < 0 || srcOffset > maxSrcBytes) {
        logWarn("[vertex_update_buffer_from_vertex] source vertex must be a positive number");
        return RValue_makeUndefined();
    }

    int32_t srcSize;
    if (srcVertNum < 0) {
        srcSize = maxSrcBytes - srcOffset;
    } else {
        srcSize = srcVertNum * vertexSize;
        if (srcOffset + srcSize > maxSrcBytes) {
            srcSize = maxSrcBytes - srcOffset;
        }
    }

    if (srcSize <= 0) {
        return RValue_makeUndefined();
    }

    int32_t destOffset = destVertex * vertexSize;
    int64_t destSize64 = (int64_t) destOffset + (int64_t) srcSize;
    if (destSize64 < 0 || destSize64 > INT32_MAX) {
        logWarn("[vertex_update_buffer_from_vertex] destination size is invalid");
        return RValue_makeUndefined();
    }

    uint32_t destSize = (uint32_t) destSize64;
    if (destSize > dest->bufferSize) {
        uint8_t* newBuffer = (uint8_t*) safeRealloc(dest->buffer.pBuffer8, (size_t) destSize);
        if (newBuffer == nullptr) {
            logError("[vertex_update_buffer_from_vertex] Memory allocation failed\n");
            return RValue_makeUndefined();
        }

        dest->buffer.pBuffer8 = newBuffer;
        dest->bufferSize = destSize;
    }

    if (srcSize > 0 && dest->buffer.pBuffer8 != nullptr) {
        memcpy(dest->buffer.pBuffer8 + destOffset, src->buffer.pBuffer8 + srcOffset, (size_t) srcSize);
    }

    if ((uint32_t) dest->vertexCount < (uint32_t) (destSize / vertexSize)) {
        dest->vertexCount = (int32_t) (destSize / vertexSize);
    }

    dest->currentFormat = -1;
    dest->streamStart = 0;
    dest->elementIndex = 0;
    dest->elementCount = destFormat->count;
    dest->currentMask = 0;
    dest->pCurrentFormatVFRelease = nullptr;

    return RValue_makeUndefined();
}

static RValue builtin_vertex_get_buffer_size(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_get_buffer_size] Illegal argument count: %d", argCount);
        return RValue_makeReal(0.0);
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        logWarn("[vertex_get_buffer_size] Vertex Buffer index is out of range");
        return RValue_makeReal(0.0);
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    if (buffer == nullptr) {
        logWarn("[vertex_get_buffer_size] Vertex Buffer index is out of range");
        return RValue_makeReal(0.0);
    }

    return RValue_makeReal((GMLReal) buffer->bufferSize);
}


static RValue builtin_vertex_get_number(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_get_number] Illegal argument count: %d", argCount);
        return RValue_makeReal(0.0);
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        logWarn("[vertex_get_number] Vertex Buffer index is out of range");
        return RValue_makeReal(0.0);
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    if (buffer == nullptr) {
        logWarn("[vertex_get_number] Vertex Buffer index is out of range");
        return RValue_makeReal(0.0);
    }

    return RValue_makeReal((GMLReal) buffer->vertexCount);
}

static RValue builtin_vertex_delete_buffer(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_delete_buffer] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        logWarn("[vertex_delete_buffer] Invalid vertex buffer specified");
        return RValue_makeUndefined();
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    if (buffer == nullptr) {
        logWarn("[vertex_delete_buffer] Invalid vertex buffer specified");
        return RValue_makeUndefined();
    }

#ifdef PLATFORM_VITA
    if (!buffer->frozen && buffer->pFrozenVB == nullptr) {
        g_VertexBuffers[bufferIndex] = nullptr;
        vitaCacheTransientVertexBuffer(buffer);
        return RValue_makeUndefined();
    }
#endif

    if (buffer->buffer.pBuffer8 != nullptr) {
        free(buffer->buffer.pBuffer8);
        buffer->buffer.pBuffer8 = nullptr;
    }

    if (buffer->pFrozenVB != nullptr) {
        if (buffer->pFrozenVB->pVertexBuffer != nullptr) {
            free(buffer->pFrozenVB->pVertexBuffer);
            buffer->pFrozenVB->pVertexBuffer = nullptr;
        }

        free(buffer->pFrozenVB);
        buffer->pFrozenVB = nullptr;
    }

    free(buffer);
    g_VertexBuffers[bufferIndex] = nullptr;
    return RValue_makeUndefined();
}


static RValue builtin_vertex_buffer_exists(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_buffer_exists] Illegal argument count: %d", argCount);
        return RValue_makeBool(false);
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        return RValue_makeBool(false);
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    return RValue_makeBool(buffer != nullptr);
}

static bool vertexBufferResolve(int32_t bufferIndex, const char* functionName, bool allowFrozen, Buffer_Vertex** outBuffer) {
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        logWarn("[%s] Illegal vertex buffer specified\n", functionName);
        return false;
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    if (buffer == nullptr || (!allowFrozen && buffer->frozen)) {
        logWarn("[%s] Illegal vertex buffer specified\n", functionName);
        return false;
    }

    vertexTrace("[%s] Resolved vertex buffer at index %d\n", functionName, bufferIndex);

    *outBuffer = buffer;
    return true;
}

static bool vertexBufferGetWritablePtr(Buffer_Vertex* buffer, int32_t* outOffset, uint8_t** outPtr, const char* functionName) {
    if (buffer == nullptr || outOffset == nullptr || outPtr == nullptr) {
        return false;
    }

    if (*outOffset < 0 || buffer->buffer.pBuffer8 == nullptr) {
        logWarn("[%s] Illegal vertex format or vertex write state\n", functionName);
        return false;
    }

    vertexTrace("[%s] Obtained writable pointer for vertex buffer at offset %d\n", functionName, *outOffset);

    *outPtr = buffer->buffer.pBuffer8 + *outOffset;
    return true;
}

static RValue builtin_vertex_begin(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 2) {
        logWarn("[vertex_begin] Illegal argument count: %d", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    int32_t formatId = RValue_toInt32(args[1]);

    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_begin", false, &buffer)) {
        return RValue_makeUndefined();
    }

    VmVertexFormat* vertexFormat = vertexFormatById(formatId);
    if (vertexFormat == nullptr) {
        logWarn("[vertex_begin] Illegal vertex format specified");
        return RValue_makeUndefined();
    }

    buffer->currentMask = 0;
    buffer->currentFormat = vertexFormat->id;
    buffer->streamStart = 0;
    buffer->streamCurrent = 0;
    buffer->vertexCount = 0;
    buffer->elementIndex = 0;
    buffer->elementCount = vertexFormat->count;
    buffer->pCurrentFormatVFRelease = vertexFormat;
    return RValue_makeUndefined();
}

static int32_t vertexBufferFindNextUsage(Buffer_Vertex* buffer, int32_t usage, enum yyVertexType type) {
    if (buffer == nullptr || buffer->pCurrentFormatVFRelease == nullptr) {
        return -1;
    }

    VmVertexFormat* vertexFormat = buffer->pCurrentFormatVFRelease;
    if (vertexFormat->format == nullptr || vertexFormat->count == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < vertexFormat->count; ++i) {
        VmVertexElement* element = &vertexFormat->format[i];
        if (((usage == -1) || (element->usage == (enum yyVertexUsage) usage)) && element->type == type) {
            if ((buffer->currentMask & element->bit) != 0) {
                return -1;
            }

            uint32_t elementSize = (uint32_t) vertexFormatTypeSize(type);
            uint32_t dataOffset = buffer->streamCurrent + element->offset;
            if (elementSize == 0 || dataOffset + elementSize > buffer->bufferSize) {
                return -1;
            }

            buffer->currentMask |= element->bit;
            if (buffer->currentMask == vertexFormat->bitMask) {
                buffer->currentMask = 0;
                buffer->vertexCount++;
                buffer->streamCurrent += vertexFormat->size;
            }

            return (int32_t) dataOffset;
        }
    }

    return -1;
}

static RValue builtin_vertex_color(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 3) {
        logWarn("[vertex_color] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_color", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUCOLOR, yyVTCOLOR);
    if (dataOffset < 0) {
        logWarn("[vertex_color] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    uint32_t color = (uint32_t) RValue_toInt32(args[1]);
    GMLReal alpha = RValue_toReal(args[2]);
    int32_t alphaInt = (int32_t) (alpha * 255.0);
    uint32_t finalColor;

    if (alphaInt > 0xff) {
        finalColor = (color & 0xffffffu) | 0xff000000u;
    } else if (alphaInt < 0) {
        finalColor = (color & 0xffffffu);
    } else {
        finalColor = (color & 0xffffffu) | ((uint32_t) alphaInt << 24);
    }

        vertexTrace("[vertex_color] Writing color 0x%08X to vertex buffer %d at offset %d (bytes: %02X %02X %02X %02X)\n",
            finalColor,
            bufferIndex,
            dataOffset,
            (unsigned int) (finalColor & 0xFFu),
            (unsigned int) ((finalColor >> 8) & 0xFFu),
            (unsigned int) ((finalColor >> 16) & 0xFFu),
            (unsigned int) ((finalColor >> 24) & 0xFFu));

    *(uint32_t*) (buffer->buffer.pBuffer8 + dataOffset) = finalColor;
    return RValue_makeUndefined();
}

static RValue builtin_vertex_normal(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 4) {
        logWarn("[vertex_normal] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_normal", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUNORMAL, yyVTFLOAT3);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_normal")) {
        logWarn("[vertex_normal] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    out[2] = (float) RValue_toReal(args[3]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_position(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 3) {
        logWarn("[vertex_position] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_position", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUPOSITION, yyVTFLOAT2);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_position")) {
        logWarn("[vertex_position] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_position_3d(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 4) {
        logWarn("[vertex_position_3d] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_position_3d", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUPOSITION, yyVTFLOAT3);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_position_3d")) {
        logWarn("[vertex_position_3d] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    out[2] = (float) RValue_toReal(args[3]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_argb(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 2) {
        logWarn("[vertex_argb] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_argb", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUCOLOR, yyVTCOLOR);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_argb")) {
        logWarn("[vertex_argb] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

        uint32_t argbColor = (uint32_t) RValue_toInt32(args[1]);
        vertexTrace("[vertex_argb] Writing ARGB color 0x%08X to vertex buffer %d at offset %d (bytes: %02X %02X %02X %02X)\n",
            argbColor,
            bufferIndex,
            dataOffset,
            (unsigned int) (argbColor & 0xFFu),
            (unsigned int) ((argbColor >> 8) & 0xFFu),
            (unsigned int) ((argbColor >> 16) & 0xFFu),
            (unsigned int) ((argbColor >> 24) & 0xFFu));

        *(uint32_t*) dst = argbColor;
    return RValue_makeUndefined();
}

static RValue builtin_vertex_texcoord(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 3) {
        logWarn("[vertex_texcoord] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_texcoord", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, yyVUTEXCOORD, yyVTFLOAT2);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_texcoord")) {
        logWarn("[vertex_texcoord] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_float1(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 2) {
        logWarn("[vertex_float1] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_float1", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, -1, yyVTFLOAT1);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_float1")) {
        logWarn("[vertex_float1] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    ((float*) dst)[0] = (float) RValue_toReal(args[1]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_float2(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 3) {
        logWarn("[vertex_float2] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_float2", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, -1, yyVTFLOAT2);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_float2")) {
        logWarn("[vertex_float2] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_float3(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 4) {
        logWarn("[vertex_float3] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_float3", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, -1, yyVTFLOAT3);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_float3")) {
        logWarn("[vertex_float3] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    out[2] = (float) RValue_toReal(args[3]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_float4(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 5) {
        logWarn("[vertex_float4] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_float4", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, -1, yyVTFLOAT4);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_float4")) {
        logWarn("[vertex_float4] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    float* out = (float*) dst;
    out[0] = (float) RValue_toReal(args[1]);
    out[1] = (float) RValue_toReal(args[2]);
    out[2] = (float) RValue_toReal(args[3]);
    out[3] = (float) RValue_toReal(args[4]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_ubyte4(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 5) {
        logWarn("[vertex_ubyte4] Illegal argument count: %d\n", argCount);
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_ubyte4", false, &buffer)) {
        return RValue_makeUndefined();
    }

    int32_t dataOffset = vertexBufferFindNextUsage(buffer, -1, yyVTUBYTE4);
    uint8_t* dst = nullptr;
    if (!vertexBufferGetWritablePtr(buffer, &dataOffset, &dst, "vertex_ubyte4")) {
        logWarn("[vertex_ubyte4] Illegal vertex format or vertex write state\n");
        return RValue_makeUndefined();
    }

    dst[0] = (uint8_t) RValue_toInt32(args[1]);
    dst[1] = (uint8_t) RValue_toInt32(args[2]);
    dst[2] = (uint8_t) RValue_toInt32(args[3]);
    dst[3] = (uint8_t) RValue_toInt32(args[4]);
    return RValue_makeUndefined();
}

static RValue builtin_vertex_end(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_end] Illegal vertex buffer specified.\n");
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_end", false, &buffer)) {
        return RValue_makeUndefined();
    }

    if (buffer->currentMask != 0) {
        logWarn("[vertex_end] Must finish writing vertex before end.\n");
        return RValue_makeUndefined();
    }

    buffer->currentMask = 0;
    buffer->pCurrentFormatVFRelease = nullptr;
    buffer->storedFormat = buffer->currentFormat;
    buffer->currentFormat = -1;
    return RValue_makeUndefined();
}

static RValue builtin_vertex_freeze(MAYBE_UNUSED VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 1) {
        logWarn("[vertex_freeze] Illegal argument count\n");
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    Buffer_Vertex* buffer = nullptr;
    if (!vertexBufferResolve(bufferIndex, "vertex_freeze", false, &buffer)) {
        return RValue_makeUndefined();
    }

    if (buffer->currentFormat != -1) {
        logWarn("[vertex_freeze] Must end the vertex builder first\n");
        return RValue_makeUndefined();
    }

    buffer->frozen = true;
    return RValue_makeUndefined();
}

static int32_t vmTypeToRendererType(enum yyVertexType type) {
    switch (type) {
        case yyVTFLOAT1: return VERTEX_TYPE_FLOAT1;
        case yyVTFLOAT2: return VERTEX_TYPE_FLOAT2;
        case yyVTFLOAT3: return VERTEX_TYPE_FLOAT3;
        case yyVTFLOAT4: return VERTEX_TYPE_FLOAT4;
        case yyVTUBYTE4: return VERTEX_TYPE_UBYTE4;
        case yyVTCOLOR:  return VERTEX_TYPE_COLOR;
        default: return -1;
    }
}

static int32_t vmUsageToRendererUsage(enum yyVertexUsage usage) {
    switch (usage) {
        case yyVUPOSITION: return VERTEX_USAGE_POSITION;
        case yyVUCOLOR:    return VERTEX_USAGE_COLOR;
        case yyVUNORMAL:   return VERTEX_USAGE_NORMAL;
        case yyVUTEXCOORD: return VERTEX_USAGE_TEXCOORD;
        default: return -1;
    }
}

static bool vertexBuildRendererFormat(VmVertexFormat* src, VertexFormat* dst) {
    if (src == nullptr || dst == nullptr || src->format == nullptr || src->count == 0) return false;

    dst->numElements = 0;
    dst->stride = src->size;

    for (uint32_t i = 0; i < src->count && dst->numElements < 16; ++i) {
        int32_t ru = vmUsageToRendererUsage(src->format[i].usage);
        int32_t rt = vmTypeToRendererType(src->format[i].type);
        if (ru < 0 || rt < 0) continue;

        VertexElement* e = &dst->elements[dst->numElements++];
        e->usage = (VertexUsage) ru;
        e->type = (VertexType) rt;
        e->offset = (uint32_t) src->format[i].offset;
        e->size = (uint32_t) vertexFormatTypeSize(src->format[i].type);
    }

    return dst->numElements > 0 && dst->stride > 0;
}

static bool vertexPrimitiveIsValid(int32_t prim, bool frozen) {
    int32_t maxExclusive = frozen ? 6 : 7;
    return prim > 0 && prim < maxExclusive;
}

static RValue builtin_vertex_submit_ext(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 5) {
        logWarn("[vertex_submit_ext] Illegal argument count\n");
        return RValue_makeUndefined();
    }

    int32_t bufferIndex = RValue_toInt32(args[0]);
    if (bufferIndex < 0 || bufferIndex >= g_VertexBufferCount) {
        logWarn("[vertex_submit_ext] Illegal vertex buffer specified.\n");
        return RValue_makeUndefined();
    }

    Buffer_Vertex* buffer = g_VertexBuffers[bufferIndex];
    if (buffer == nullptr) {
        logWarn("[vertex_submit_ext] Illegal vertex buffer specified.\n");
        return RValue_makeUndefined();
    }

    if (buffer->storedFormat == -1) {
        logWarn("[vertex_submit_ext] Must finish with vertex_end() before submitting.\n");
        return RValue_makeUndefined();
    }

    int32_t primitive = RValue_toInt32(args[1]);
    if (!vertexPrimitiveIsValid(primitive, buffer->frozen)) {
        logWarn("[vertex_submit_ext] Primitive type error.\n");
        return RValue_makeUndefined();
    }

    VmVertexFormat* vmFormat = vertexFormatById((uint32_t) buffer->storedFormat);
    if (vmFormat == nullptr || vmFormat->size == 0 || vmFormat->format == nullptr) {
        logWarn("[vertex_submit_ext] Illegal vertex format.\n");
        return RValue_makeUndefined();
    }

    VertexFormat tempFormat;
    if (!vertexBuildRendererFormat(vmFormat, &tempFormat)) {
        logWarn("[vertex_submit_ext] Illegal vertex format.\n");
        return RValue_makeUndefined();
    }

    VertexBuffer tempBuffer;
    memset(&tempBuffer, 0, sizeof(tempBuffer));
    tempBuffer.data = buffer->buffer.pBuffer8;
    tempBuffer.size = (size_t) ((buffer->vertexCount > 0) ? ((uint32_t) buffer->vertexCount * vmFormat->size) : buffer->streamCurrent);
    tempBuffer.capacity = buffer->bufferSize;
    tempBuffer.format = &tempFormat;
    tempBuffer.vertexSize = vmFormat->size;
    tempBuffer.isFrozen = buffer->frozen;
    tempBuffer.rendererData = buffer->pFrozenVB ? buffer->pFrozenVB->pVertexBuffer : nullptr;

    int32_t texture = RValue_toInt32(args[2]);
    int32_t offset = RValue_toInt32(args[3]);
    int32_t number = RValue_toInt32(args[4]);

    if (ctx != nullptr && ctx->runner != nullptr && ctx->runner->renderer != nullptr &&
        ctx->runner->renderer->vtable != nullptr && ctx->runner->renderer->vtable->drawVertexBuffer != nullptr) {
        ctx->runner->renderer->vtable->drawVertexBuffer(
            ctx->runner->renderer,
            &tempBuffer,
            primitive - 1,
            texture,
            offset,
            number
        );

        if (tempBuffer.rendererData != nullptr) {
            if (buffer->pFrozenVB == nullptr) {
                buffer->pFrozenVB = (VmVertexBuffer*) safeMalloc(sizeof(VmVertexBuffer));
                if (buffer->pFrozenVB != nullptr) {
                    memset(buffer->pFrozenVB, 0, sizeof(VmVertexBuffer));
                }
            }

            if (buffer->pFrozenVB != nullptr) {
                buffer->pFrozenVB->pVertexBuffer = tempBuffer.rendererData;
            }
        }
    }

    return RValue_makeUndefined();
}

static RValue builtin_vertex_submit(VMContext* ctx, RValue* args, int32_t argCount) {
    if (argCount != 3) {
        logWarn("[vertex_submit] Illegal argument count\n");
        return RValue_makeUndefined();
    }

    RValue extArgs[5];
    extArgs[0] = args[0];
    extArgs[1] = args[1];
    extArgs[2] = args[2];
    extArgs[3] = RValue_makeInt32(0);
    extArgs[4] = RValue_makeInt32(-1);
    return builtin_vertex_submit_ext(ctx, extArgs, 5);
}





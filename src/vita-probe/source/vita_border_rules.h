#ifndef DELTARUNE_VITA_BORDER_RULES_H
#define DELTARUNE_VITA_BORDER_RULES_H

typedef enum VitaBorderAsset {
    VITA_BORDER_ASSET_NONE = 0,
    VITA_BORDER_ASSET_LINE_1080,
    VITA_BORDER_ASSET_LW_TOWN,
    VITA_BORDER_ASSET_LW_TOWN_MORNING,
    VITA_BORDER_ASSET_LW_TOWN_SUNSET,
    VITA_BORDER_ASSET_LW_TOWN_NIGHT,
    VITA_BORDER_ASSET_DW_CASTLETOWN,
    VITA_BORDER_ASSET_DW_CYBER,
    VITA_BORDER_ASSET_DW_CITY,
    VITA_BORDER_ASSET_DW_MANSION,
    VITA_BORDER_ASSET_DW_TV_BLACK,
    VITA_BORDER_ASSET_DW_BLUE_STARS,
    VITA_BORDER_ASSET_DW_GREEN_ROOM,
    VITA_BORDER_ASSET_DW_RED_SMILES,
    VITA_BORDER_ASSET_DW_TEEVIE,
    VITA_BORDER_ASSET_DW_BLUE_LIGHT,
    VITA_BORDER_ASSET_DW_TV_META,
    VITA_BORDER_ASSET_DW_TV_BLUE,
    VITA_BORDER_ASSET_DW_GREEN_SLOPPY,
    VITA_BORDER_ASSET_DW_GREEN_SLOPPY_Z,
    VITA_BORDER_ASSET_DW_WORD,
    VITA_BORDER_ASSET_DW_CHURCH_A,
    VITA_BORDER_ASSET_DW_CHURCH_B,
    VITA_BORDER_ASSET_DW_CHURCH_C,
    VITA_BORDER_ASSET_DW_TITAN_BASE,
    VITA_BORDER_ASSET_DW_TITAN_EYES,
    VITA_BORDER_ASSET_DW_GARDEN,
    VITA_BORDER_ASSET_DW_GARDEN_CLIFF,
    VITA_BORDER_ASSET_DW_CASTLE_RIGHT,
    VITA_BORDER_ASSET_DW_CASTLE_LEFT,
    VITA_BORDER_ASSET_DW_CASTLE_RIGHT_GOLD,
    VITA_BORDER_ASSET_DW_CASTLE_TOP,
    VITA_BORDER_ASSET_DW_CASTLE_CAFE,
    VITA_BORDER_ASSET_DW_PINK,
    VITA_BORDER_ASSET_DW_PINK_ALT
} VitaBorderAsset;

typedef enum VitaBorderSceneFlags {
    VITA_BORDER_SCENE_NONE = 0,
    VITA_BORDER_SCENE_CH3_BLUE_STARS = 1u << 0,
    VITA_BORDER_SCENE_CH3_RED_SMILES = 1u << 1,
    VITA_BORDER_SCENE_CH3_TV_BLACK = 1u << 2,
    VITA_BORDER_SCENE_CH3_WORD = 1u << 3,
    VITA_BORDER_SCENE_CH3_KNIGHT_NEUTRAL = 1u << 4,
    VITA_BORDER_SCENE_CH5_PINK_ALT = 1u << 8,
    VITA_BORDER_SCENE_CH5_CASTLE_GOLD = 1u << 9,
    VITA_BORDER_SCENE_CH5_CASTLE_TOP = 1u << 10,
    VITA_BORDER_SCENE_CH5_GARDEN_REVEAL = 1u << 11,
    VITA_BORDER_SCENE_HIDE_WHITE = 1u << 16,
    VITA_BORDER_SCENE_HIDE_BLACK = 1u << 17
} VitaBorderSceneFlags;
typedef struct VitaBorderRuleState {
    int chapter;
    int roomIndex;
    int plot;
    int darkzone;
    int flag1324;
    int gameWon;
    unsigned int sceneFlags;
} VitaBorderRuleState;

typedef enum VitaBorderSource {
    VITA_BORDER_SOURCE_FALLBACK = 0,
    VITA_BORDER_SOURCE_CONTROLLER = 1,
    VITA_BORDER_SOURCE_BETTER = 2,
    VITA_BORDER_SOURCE_CUSTOM = 3
} VitaBorderSource;
typedef struct VitaBorderDecision {
    VitaBorderAsset asset;
    int overlayTarget;
    float overlaySpeed;
    unsigned int overlayColor;
} VitaBorderDecision;

VitaBorderDecision VitaBorderRules_resolve(const char* room, const VitaBorderRuleState* state);
const char* VitaBorderRules_filename(VitaBorderAsset asset);
VitaBorderSource VitaBorderRules_chooseSource(int hasCustom, int hasBetterBorder, int hasNativeController);
int VitaBorderRules_controllerCrossfade(int chapter, int currentSpriteId, int pendingSpriteId, int explicitCrossfade);
int VitaBorderRules_isMenuRoom(const char* roomName);
int VitaBorderRules_isChapter3BoardSwitchOwner(const char* objectName);
int VitaBorderRules_shouldBlankChapter5Surround(int chapter, const char* roomName, unsigned int sceneFlags);
int VitaBorderRules_shouldBlankSurround(int chapter, const char* roomName, unsigned int sceneFlags);

#endif



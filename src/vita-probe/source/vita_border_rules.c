#include "vita_border_rules.h"

#include <stddef.h>
#include <string.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

static int room_eq(const char* room, const char* name) {
    return room != NULL && name != NULL && strcmp(room, name) == 0;
}

static int room_prefix(const char* room, const char* prefix) {
    return room != NULL && prefix != NULL && strncmp(room, prefix, strlen(prefix)) == 0;
}

static int room_in(const char* room, const char* const* rooms, size_t count) {
    if (room == NULL) return 0;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(room, rooms[i]) == 0) return 1;
    return 0;
}

VitaBorderSource VitaBorderRules_chooseSource(int hasCustom, int hasBetterBorder, int hasNativeController) {
    if (hasCustom) return VITA_BORDER_SOURCE_CUSTOM;
    if (hasBetterBorder) return VITA_BORDER_SOURCE_BETTER;
    if (hasNativeController) return VITA_BORDER_SOURCE_CONTROLLER;
    return VITA_BORDER_SOURCE_FALLBACK;
}

int VitaBorderRules_isMenuRoom(const char* roomName) {
    if (roomName == NULL) return 0;
    return strcmp(roomName, "PLACE_LOGO") == 0 || strcmp(roomName, "room_legend") == 0 ||
           strstr(roomName, "MENU") != NULL || strstr(roomName, "menu") != NULL ||
           strstr(roomName, "CHAPTER_SELECT") != NULL || strstr(roomName, "chapter_select") != NULL ||
           strstr(roomName, "SELECT") != NULL || strstr(roomName, "select") != NULL;
}

int VitaBorderRules_isChapter3BoardSwitchOwner(const char* objectName) {
    return objectName != NULL && strcmp(objectName, "obj_ch3_GSB01") == 0;
}

int VitaBorderRules_shouldBlankChapter5Surround(int chapter, const char* roomName, unsigned int sceneFlags) {
    if (chapter != 5 || roomName == NULL) return 0;
    if (room_eq(roomName, "room_dw_garden_meetflowery") ||
        room_eq(roomName, "room_dw_garden_video") ||
        room_eq(roomName, "room_dw_garden_intro") ||
        room_eq(roomName, "room_dw_garden_ralseicupboard"))
        return 1;
    if (room_eq(roomName, "room_dw_garden_floradinnencounter"))
        return (sceneFlags & VITA_BORDER_SCENE_CH5_GARDEN_REVEAL) == 0;
    return 0;
}

int VitaBorderRules_shouldBlankSurround(int chapter, const char* roomName, unsigned int sceneFlags) {
    if (chapter <= 0 || roomName == NULL) return 0;
    // The first Dark World room is the black approach to Castle Town. The
    // decorative Castletown surround starts only in room_dw_castle_town.
    if (room_eq(roomName, "room_dw_castle_area_1")) return 1;
    if (chapter == 3 && room_eq(roomName, "room_ed")) return 1;
    if (chapter == 4 && room_eq(roomName, "room_cc_fountain")) return 1;
    return VitaBorderRules_shouldBlankChapter5Surround(chapter, roomName, sceneFlags);
}
const char* VitaBorderRules_filename(VitaBorderAsset asset) {
    switch (asset) {
        case VITA_BORDER_ASSET_LINE_1080: return "border_line_1080_0.png";
        case VITA_BORDER_ASSET_LW_TOWN: return "border_lw_town_0.png";
        case VITA_BORDER_ASSET_LW_TOWN_MORNING: return "border_lw_town_morning_0.png";
        case VITA_BORDER_ASSET_LW_TOWN_SUNSET: return "border_lw_town_sunset_0.png";
        case VITA_BORDER_ASSET_LW_TOWN_NIGHT: return "border_lw_town_night_0.png";
        case VITA_BORDER_ASSET_DW_CASTLETOWN: return "border_dw_castletown_0.png";
        case VITA_BORDER_ASSET_DW_CYBER: return "border_dw_cyber_0.png";
        case VITA_BORDER_ASSET_DW_CITY: return "border_dw_city_0.png";
        case VITA_BORDER_ASSET_DW_MANSION: return "border_dw_mansion_0.png";
        case VITA_BORDER_ASSET_DW_TV_BLACK: return "border_dw_tv_black_0.png";
        case VITA_BORDER_ASSET_DW_BLUE_STARS: return "border_dw_blue_stars_0.png";
        case VITA_BORDER_ASSET_DW_GREEN_ROOM: return "border_dw_green_room_0.png";
        case VITA_BORDER_ASSET_DW_RED_SMILES: return "border_dw_red_smiles_0.png";
        case VITA_BORDER_ASSET_DW_TEEVIE: return "border_dw_teevie_0.png";
        case VITA_BORDER_ASSET_DW_BLUE_LIGHT: return "border_dw_blue_light_0.png";
        case VITA_BORDER_ASSET_DW_TV_META: return "border_dw_tv_meta_0.png";
        case VITA_BORDER_ASSET_DW_TV_BLUE: return "border_dw_tv_blue_0.png";
        case VITA_BORDER_ASSET_DW_GREEN_SLOPPY: return "border_dw_green_sloppy_0.png";
        case VITA_BORDER_ASSET_DW_GREEN_SLOPPY_Z: return "border_dw_green_sloppy_z_0.png";
        case VITA_BORDER_ASSET_DW_WORD: return "border_dw_word_0.png";
        case VITA_BORDER_ASSET_DW_CHURCH_A: return "border_dw_church_a_0.png";
        case VITA_BORDER_ASSET_DW_CHURCH_B: return "border_dw_church_b_0.png";
        case VITA_BORDER_ASSET_DW_CHURCH_C: return "border_dw_church_c_0.png";
        case VITA_BORDER_ASSET_DW_TITAN_BASE: return "border_dw_titan_base_0.png";
        case VITA_BORDER_ASSET_DW_TITAN_EYES: return "border_dw_titan_eyes_0.png";
        case VITA_BORDER_ASSET_DW_GARDEN: return "border_dw_garden_0.png";
        case VITA_BORDER_ASSET_DW_GARDEN_CLIFF: return "border_dw_garden_cliff_0.png";
        case VITA_BORDER_ASSET_DW_CASTLE_RIGHT: return "border_dw_castle_right_0.png";
        case VITA_BORDER_ASSET_DW_CASTLE_LEFT: return "border_dw_castle_left_0.png";
        case VITA_BORDER_ASSET_DW_CASTLE_RIGHT_GOLD: return "border_dw_castle_right_gold_0.png";
        case VITA_BORDER_ASSET_DW_CASTLE_TOP: return "border_dw_castle_top_0.png";
        case VITA_BORDER_ASSET_DW_CASTLE_CAFE: return "border_dw_castle_cafe_0.png";
        case VITA_BORDER_ASSET_DW_PINK: return "border_dw_pink_0.png";
        case VITA_BORDER_ASSET_DW_PINK_ALT: return "border_dw_pink_alt_0.png";
        default: return NULL;
    }
}
int VitaBorderRules_controllerCrossfade(int chapter, int currentSpriteId, int pendingSpriteId, int explicitCrossfade) {
    if (chapter < 3 || chapter > 5) return 0;
    if (currentSpriteId >= 0 && pendingSpriteId >= 0 && currentSpriteId != pendingSpriteId) return 1;
    return chapter == 5 && explicitCrossfade ? 1 : 0;
}

static VitaBorderAsset resolve_ch1(const char* room, const VitaBorderRuleState* s) {
    (void)room;
    return s->darkzone ? VITA_BORDER_ASSET_DW_CASTLETOWN : VITA_BORDER_ASSET_LW_TOWN;
}

static VitaBorderAsset resolve_ch2(const char* room, const VitaBorderRuleState* s) {
    VitaBorderAsset asset = s->darkzone ? VITA_BORDER_ASSET_DW_CASTLETOWN : VITA_BORDER_ASSET_LW_TOWN;
    if (room_eq(room, "room_legend") || room_eq(room, "PLACE_MENU") || room_eq(room, "PLACE_LOGO"))
        asset = VITA_BORDER_ASSET_DW_CASTLETOWN;

    // Match the original GameMaker resource-ID ranges exactly. Prefix-based
    // matching is insufficient: room_dw_city_prototype_01/02 are resource
    // IDs 106/107 and still belong to the Cyber interval, while City begins
    // at room_dw_city_spamton_shop_exterior (108).
    if (s->roomIndex >= 74 && s->roomIndex < 108)
        asset = VITA_BORDER_ASSET_DW_CYBER;
    if (s->roomIndex >= 108 && s->roomIndex < 150)
        asset = VITA_BORDER_ASSET_DW_CITY;
    if ((s->roomIndex >= 150 && s->roomIndex <= 210) ||
        room_eq(room, "room_dw_mansion_prefountain") || room_eq(room, "room_dw_mansion_fountain"))
        asset = VITA_BORDER_ASSET_DW_MANSION;

    // Fallback for unit tests/diagnostics that do not provide a room index.
    if (s->roomIndex < 0) {
        if (room_prefix(room, "room_dw_cyber_")) asset = VITA_BORDER_ASSET_DW_CYBER;
        if (room_prefix(room, "room_dw_city_")) asset = VITA_BORDER_ASSET_DW_CITY;
        if (room_prefix(room, "room_dw_mansion_") || room_prefix(room, "room_transformation_sequence"))
            asset = VITA_BORDER_ASSET_DW_MANSION;
    }

    if (room_eq(room, "room_shop_ch2_music") && s->plot < 200) asset = VITA_BORDER_ASSET_DW_CYBER;
    if (room_eq(room, "room_shop_ch2_swatch")) asset = VITA_BORDER_ASSET_DW_MANSION;
    if (room_eq(room, "room_shop_ch2_spamton")) asset = VITA_BORDER_ASSET_DW_CITY;
    return asset;
}

static VitaBorderAsset resolve_ch3(const char* room, const VitaBorderRuleState* s) {
    VitaBorderAsset asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (s->plot >= 70) asset = VITA_BORDER_ASSET_DW_BLUE_STARS;
    if (s->plot >= 120) asset = VITA_BORDER_ASSET_DW_GREEN_ROOM;
    if (s->plot >= 122 && room_eq(room, "room_ch3_gameshowroom")) asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (s->plot >= 160) asset = VITA_BORDER_ASSET_DW_GREEN_ROOM;
    if (s->plot >= 230) asset = VITA_BORDER_ASSET_DW_RED_SMILES;
    if (s->plot >= 250) asset = VITA_BORDER_ASSET_DW_TEEVIE;
    if (s->plot >= 290) asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (s->plot >= 308) asset = VITA_BORDER_ASSET_DW_BLUE_STARS;
    if (s->plot >= 310) asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (s->plot >= 340) asset = VITA_BORDER_ASSET_LW_TOWN_NIGHT;

    if (room_eq(room, "room_legend") || room_eq(room, "PLACE_MENU") || room_eq(room, "PLACE_LOGO"))
        asset = VITA_BORDER_ASSET_DW_CASTLETOWN;
    if (room_eq(room, "room_dw_couch_overworld_intro") || room_eq(room, "room_dw_couch_overworld_05"))
        asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (room_eq(room, "room_dw_tv_curtain") || room_eq(room, "room_dw_chef") ||
        room_eq(room, "room_dw_rhythm") || room_eq(room, "room_dw_ranking_a") ||
        room_eq(room, "room_dw_rhythm_countdown") || room_eq(room, "room_dw_susiezilla"))
        asset = VITA_BORDER_ASSET_DW_BLUE_STARS;
    if (room_eq(room, "room_board_gsa02_b0") || room_eq(room, "room_board_intro") ||
        room_eq(room, "room_board_1") || room_eq(room, "room_board_2"))
        asset = VITA_BORDER_ASSET_DW_TV_META;
    if (room_eq(room, "room_dw_b3bs_interstitial") || room_eq(room, "room_dw_inbetween") ||
        room_eq(room, "room_dw_ranking_hub"))
        asset = s->plot >= 280 ? VITA_BORDER_ASSET_DW_BLUE_LIGHT : VITA_BORDER_ASSET_DW_GREEN_ROOM;
    if (room_prefix(room, "room_dw_puzzlecloset_")) asset = VITA_BORDER_ASSET_DW_TV_BLUE;
    if (room_eq(room, "room_dw_teevie_intro") || room_eq(room, "room_dw_teevie_chef") ||
        room_eq(room, "room_dw_b3bs_jail1") || room_eq(room, "room_dw_teevie_rhythm"))
        asset = VITA_BORDER_ASSET_DW_TEEVIE;
    if (room_eq(room, "room_dw_changing_room")) asset = VITA_BORDER_ASSET_DW_GREEN_SLOPPY;
    if (room_eq(room, "room_dw_ranking_z_hallway") || room_eq(room, "room_dw_ranking_z"))
        asset = VITA_BORDER_ASSET_DW_GREEN_SLOPPY_Z;
    if (room_eq(room, "room_town_krisyard_dark")) asset = VITA_BORDER_ASSET_LW_TOWN_NIGHT;
    if (room_eq(room, "room_dw_snow_zone")) asset = VITA_BORDER_ASSET_DW_TV_BLACK;
    if (room_eq(room, "room_dw_snow_zone_battle")) asset = VITA_BORDER_ASSET_DW_BLUE_STARS;
    return asset;
}

static const char* const ch4_castle_rooms[] = {
    "room_legend", "PLACE_MENU", "PLACE_LOGO", "room_dw_castle_area_1",
    "room_dw_castle_rooms_kris_susie", "room_castle_tutorial", "room_dw_castle_east_door",
    "room_dw_castle_west_cliff", "room_dw_ralsei_castle_front", "room_dw_castle_restaurant",
    "room_dw_castle_cafe", "room_dw_castle_dojo", "room_dw_ralsei_castle_1f",
    "room_dw_ralsei_castle_2f", "room_dw_ralsei_castle_3f", "room_dw_castle_dungeon",
    "room_dw_castle_rooms_kris", "room_dw_castle_rooms_susie", "room_dw_castle_rooms_ralsei",
    "room_dw_castle_rooms_queen", "room_dw_castle_rooms_tenna", "room_dw_castle_tv_rhythm",
    "room_dw_castle_tv_zone_1", "room_dw_castle_tv_zone_2", "room_dw_castle_tv_zone_3",
    "room_dw_castle_tv_zone_battle", "room_dw_castle_tv_zone_minigame", "room_dw_castle_tv",
    "room_dw_castle_town", "room_dw_rhythm", "room_dw_rhythm_countdown", "room_dw_rhythm_empty"
};

static const char* const ch4_church_a_rooms[] = {
    "room_dw_church_savepoint", "room_dw_church_intro1", "room_dw_church_intropiano",
    "room_dw_church_staircase", "room_dw_church_glass", "room_dw_church_holywatercooler",
    "room_dw_church_intro_guei", "room_dw_church_intro_gerson", "room_dw_church_shadowgerson",
    "room_dw_church_minorlegend", "room_dw_church_bookcase", "room_dw_church_turtles",
    "room_dw_church_ripplepuzzle", "room_dw_church_ripseq1", "room_dw_church_ripseq2",
    "room_dw_church_lantern_hallway", "room_dw_church_biblioxencounter",
    "room_dw_church_ripplepuzzle_postgers", "room_dw_church_gersonstudy", "room_dw_church_nwconnect",
    "room_dw_church_moneyfountain", "room_dw_church_bookshelfpuzzle", "room_dw_church_tallbookcases",
    "room_dw_church_pianopiece_left", "room_dw_church_pianopiece_right", "room_dw_church_pianopuzzle",
    "room_dw_church_jackenstein", "room_dw_church_knightclimb_post", "room_dw_church_darkmaze",
    "room_dw_church_rightconnect", "room_dw_church_stairspreview", "room_dw_church_trueclimbadventure",
    "room_dw_church_organpuzzle", "room_dw_church_mizzleencounter", "room_dw_church_bellhall_bookroom",
    "room_dw_church_bellhall_curtain", "room_dw_church_sideclimb", "room_dw_church_secretpiano",
    "room_dw_church_worshiproom", "room_dw_church_fountain", "room_dw_church_northprophecies",
    "room_dw_church_pianopiece_left_b", "room_dw_church_pianopiece_rightprophecy",
    "room_dw_church_rippleworship", "room_dw_church_waterfallroom", "room_dw_church_waterfalltearoom"
};
static const char* const ch4_church_b_rooms[] = {
    "room_dw_churchb_ripple1", "room_dw_churchb_ripplepost", "room_dw_churchb_gersonstudy",
    "room_dw_churchb_staircaseintro", "room_dw_churchb_savepoint", "room_dw_churchb_darkclimb"
};
static const char* const ch4_church_c_rooms[] = {
    "room_dw_churchc_superprophecies", "room_dw_churchc_savepoint",
    "room_dw_churchc_prepretitan", "room_dw_churchc_finalclimb"
};
static VitaBorderAsset resolve_ch4(const char* room, const VitaBorderRuleState* s) {
    VitaBorderAsset asset = s->darkzone ? VITA_BORDER_ASSET_DW_CASTLETOWN : VITA_BORDER_ASSET_LW_TOWN;
    if (s->plot > 105) asset = VITA_BORDER_ASSET_DW_CHURCH_A;
    if (s->plot >= 225) asset = VITA_BORDER_ASSET_DW_CHURCH_B;
    if (s->plot >= 240) asset = VITA_BORDER_ASSET_DW_CHURCH_C;
    if (s->plot >= 300) asset = VITA_BORDER_ASSET_LW_TOWN_NIGHT;
    if (room_in(room, ch4_castle_rooms, COUNT_OF(ch4_castle_rooms))) asset = VITA_BORDER_ASSET_DW_CASTLETOWN;
    if ((room_eq(room, "room_torhouse") || room_eq(room, "room_krisroom") ||
         room_eq(room, "room_town_mid") || room_eq(room, "room_lw_noellehouse_kitchen")) && s->plot < 290)
        asset = VITA_BORDER_ASSET_LW_TOWN;
    if (room_in(room, ch4_church_a_rooms, COUNT_OF(ch4_church_a_rooms)))
        asset = VITA_BORDER_ASSET_DW_CHURCH_A;
    if (room_in(room, ch4_church_b_rooms, COUNT_OF(ch4_church_b_rooms)))
        asset = VITA_BORDER_ASSET_DW_CHURCH_B;
    if (room_in(room, ch4_church_c_rooms, COUNT_OF(ch4_church_c_rooms)))
        asset = VITA_BORDER_ASSET_DW_CHURCH_C;
    if (room_eq(room, "room_dw_churchc_titanclimb1") || room_eq(room, "room_dw_churchc_titanclimb1_post") ||
        room_eq(room, "room_dw_churchc_titanclimb2") || room_eq(room, "room_dw_churchc_titanclimb2_post"))
        asset = VITA_BORDER_ASSET_DW_TITAN_BASE;
    if (room_eq(room, "room_town_church") && s->plot >= 290) asset = VITA_BORDER_ASSET_LW_TOWN_NIGHT;
    return asset;
}

static const char* const ch5_lw_rooms[] = {
    "room_town_krisyard", "room_town_northwest", "room_town_north", "room_beach", "room_town_mid",
    "room_town_apartments", "room_town_south", "room_town_school", "room_town_church", "room_graveyard",
    "room_town_shelter", "room_town_noellehouse", "room_lw_church_main", "room_torielclass", "room_schoollobby",
    "room_alphysclass", "room_schooldoor", "room_school_unusedroom"
};
static const char* const ch5_garden_rooms[] = {
    "room_dw_garden_floradinnencounter", "room_dw_garden_hospital", "room_dw_garden_aqua",
    "room_dw_garden_finalplatforming", "room_dw_garden_diner", "room_dw_garden_aquahole",
    "room_dw_garden_aquadarkness", "room_dw_garden_aquashrine", "room_dw_garden_enemyrush",
    "room_dw_garden_starwalkerdash", "room_dw_garden_wateringcan_aqua", "room_dw_garden_aquadash",
    "room_dw_garden_platshortcut", "room_dw_garden_ralseicupboard", "room_dw_garden_aquaplatforming",
    "room_dw_garden_shearydodge", "room_dw_garden_fishingspot", "room_dw_garden_riverchest",
    "room_dw_garden_pedestal", "room_dw_garden_flowerygardening", "room_dw_garden_mushrooms",
    "room_dw_garden_susiechase", "room_dw_garden_finalplatforming_right", "room_dw_garden_hardpressureplates",
    "room_dw_garden_aquatransition", "room_dw_garden_shearyguide", "room_dw_garden_newdash",
    "room_dw_garden_aquahole_left", "room_dw_garden_firstdash", "room_dw_garden_aquadash_plat",
    "room_dw_garden_hopschef"
};
static const char* const ch5_garden_dark_rooms[] = {
    "room_dw_garden_aquatransition", "room_dw_garden_aquadash", "room_dw_garden_aquadash_plat",
    "room_dw_garden_finalplatforming_right", "room_dw_garden_wateringcan_aqua", "room_dw_garden_aqua",
    "room_dw_garden_aquadarkness", "room_dw_garden_aquahole", "room_dw_garden_aquahole_left",
    "room_dw_garden_aquashrine", "room_dw_garden_aquaplatforming", "room_dw_garden_starwalkerdash"
};
static const char* const ch5_cliff_rooms[] = {
    "room_dw_garden_cliffexit", "room_dw_cliff_shop", "room_dw_cliff_cutdown_tutorial",
    "room_dw_cliff_dash_runner", "room_dw_cliff_seth_miniboss", "room_dw_cliff_silver_hammer",
    "room_dw_cliff_yellowcave", "room_dw_cliff_shicave", "room_dw_cliff_verticalwind",
    "room_dw_cliff_gardentransition_new", "room_dw_cliff_netskieclimb", "room_dw_cliff_finaldash",
    "room_dw_cliff_bunnyfarm", "room_dw_cliff_verticalwind_post", "room_dw_cliff_netskieclimb_behind",
    "room_dw_cliff_eastcliff", "room_dw_cliff_bonuscombat", "room_dw_cliff_twirlflowerplatforming",
    "room_dw_cliff_kawkawdash", "room_dw_cliff_precipice", "room_dw_cliff_twirlflowerwind",
    "room_dw_cliff_climbrefresher", "room_dw_cliff_sethaqua_battle", "room_dw_fcastle_heldmushrooms",
    "room_dw_fcastle_right_penultimate", "room_dw_fcastle_left_penultimate", "room_dw_fcastle_flowerydash",
    "room_dw_fcastle_shinobeetle_3d"
};
static const char* const ch5_castle_right[] = {
    "room_dw_fcastle_foyer", "room_dw_fcastle_foxhunt", "room_dw_fcastle_gloves_tower",
    "room_dw_fcastle_green_orange_battle", "room_dw_fcastle_obscured_bullets",
    "room_dw_fcastle_orange_puppet_introduction", "room_dw_fcastle_right_endingscene",
    "room_dw_fcastle_right_puzzle", "room_dw_fcastle_right_wing_floweryscene", "room_dw_fcastle_second_diner",
    "room_dw_fcastle_sidepuzzle", "room_dw_fcastle_terracotta_bonus", "room_dw_fcastle_terracotta_encounter",
    "room_dw_fcastle_trainroom", "room_dw_fcastle_terracotta_puzzle", "room_dw_fcastle_foxhunt_terakota",
    "room_dw_fcastle_foxhunt_socks", "room_dw_fcastle_foxhunt_chaos", "room_dw_fcastle_foxhunt_secret",
    "room_dw_fcastle_fusumadodge"
};
static const char* const ch5_castle_left[] = {
    "room_dw_fcastle_cafe", "room_dw_fcastle_blueroom", "room_dw_fcastle_bounce_1", "room_dw_fcastle_bounce_3",
    "room_dw_fcastle_dangerous_platforming", "room_dw_fcastle_left_twodoors", "room_dw_fcastle_left_wing_floweryscene",
    "room_dw_fcastle_onsen", "room_dw_fcastle_sandtrap", "room_dw_fcastle_shinobeetle_encounter",
    "room_dw_fcastle_yellow_miniboss", "room_dw_fcastle_yellowjail", "room_dw_fcastle_zenlooker"
};
static const char* const ch5_castle_gold[] = {
    "room_dw_fcastle_top_pinkdoor", "room_dw_fcastle_green_checkpoint", "room_dw_fcastle_flowery",
    "room_dw_post_fountain_close", "room_dw_fcastle_top_staircase_1", "room_dw_fcastle_top_staircase_2",
    "room_dw_fcastle_final_save"
};
static const char* const ch5_castle_top[] = {
    "room_dw_fcastle_top_entrance", "room_dw_fcastle_top_fountain", "room_dw_flowery_tree",
    "room_dw_fcastle_ultradash", "room_dw_fcastle_yellowblue", "room_dw_fcastle_seth_encounter",
    "room_dw_fcastle_top_ascent", "room_dw_fcastle_top_challenge", "room_dw_fcastle_top_descent",
    "room_dw_fcastle_orange_gauntlet"
};

static VitaBorderAsset resolve_ch5(const char* room, const VitaBorderRuleState* s) {
    VitaBorderAsset asset = s->darkzone ? VITA_BORDER_ASSET_DW_CASTLETOWN : VITA_BORDER_ASSET_LW_TOWN;
    if (room_in(room, ch5_lw_rooms, COUNT_OF(ch5_lw_rooms))) {
        asset = VITA_BORDER_ASSET_LW_TOWN;
        if (s->flag1324 == 0) asset = VITA_BORDER_ASSET_LW_TOWN_MORNING;
        else if (s->flag1324 == 2) asset = VITA_BORDER_ASSET_LW_TOWN_SUNSET;
        else if (s->flag1324 == 3) asset = VITA_BORDER_ASSET_LW_TOWN_NIGHT;
    }
    if (room_in(room, ch5_garden_rooms, COUNT_OF(ch5_garden_rooms))) asset = VITA_BORDER_ASSET_DW_GARDEN;
    if (room_in(room, ch5_garden_dark_rooms, COUNT_OF(ch5_garden_dark_rooms)) ||
        room_in(room, ch5_cliff_rooms, COUNT_OF(ch5_cliff_rooms))) asset = VITA_BORDER_ASSET_DW_GARDEN_CLIFF;
    if (room_in(room, ch5_castle_right, COUNT_OF(ch5_castle_right))) asset = VITA_BORDER_ASSET_DW_CASTLE_RIGHT;
    if (room_in(room, ch5_castle_left, COUNT_OF(ch5_castle_left))) asset = VITA_BORDER_ASSET_DW_CASTLE_LEFT;
    if (room_in(room, ch5_castle_gold, COUNT_OF(ch5_castle_gold))) asset = VITA_BORDER_ASSET_DW_CASTLE_RIGHT_GOLD;
    if (room_in(room, ch5_castle_top, COUNT_OF(ch5_castle_top))) asset = VITA_BORDER_ASSET_DW_CASTLE_TOP;
    if (room_eq(room, "room_dw_fcastle_cafe")) asset = VITA_BORDER_ASSET_DW_CASTLE_CAFE;
    if (room_eq(room, "room_dw_pink_encounter")) asset = VITA_BORDER_ASSET_DW_PINK;
    return asset;
}

VitaBorderDecision VitaBorderRules_resolve(const char* room, const VitaBorderRuleState* state) {
    VitaBorderDecision d;
    d.asset = VITA_BORDER_ASSET_LINE_1080;
    d.overlayTarget = 0;
    d.overlaySpeed = 0.025f;
    d.overlayColor = 0x000000U;
    if (state == NULL) return d;
    if (state->sceneFlags & VITA_BORDER_SCENE_HIDE_WHITE) {
        d.overlayTarget = 1;
        d.overlaySpeed = 1.0f / 30.0f;
        d.overlayColor = 0xFFFFFFU;
    } else if (state->sceneFlags & VITA_BORDER_SCENE_HIDE_BLACK) {
        d.overlayTarget = 1;
        d.overlaySpeed = 0.025f;
        d.overlayColor = 0x000000U;
    }
    if (state->chapter == 3) {
        if (state->sceneFlags & VITA_BORDER_SCENE_CH3_KNIGHT_NEUTRAL)
            d.asset = VITA_BORDER_ASSET_LINE_1080;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH3_WORD)
            d.asset = VITA_BORDER_ASSET_DW_WORD;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH3_TV_BLACK)
            d.asset = VITA_BORDER_ASSET_DW_TV_BLACK;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH3_RED_SMILES)
            d.asset = VITA_BORDER_ASSET_DW_RED_SMILES;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH3_BLUE_STARS)
            d.asset = VITA_BORDER_ASSET_DW_BLUE_STARS;
        else
            d.asset = resolve_ch3(room, state);
        return d;
    }
    if (state->chapter == 5) {
        d.asset = resolve_ch5(room, state);
        // This opening sequence deliberately keeps the console surround black.
        // The first Garden border is only revealed once the sakura transition
        // reaches stage 1, matching obj_border_controller.show_border(1/90).
        if (room_eq(room, "room_dw_garden_meetflowery") ||
            room_eq(room, "room_dw_garden_intro") ||
            room_eq(room, "room_dw_garden_ralseicupboard")) {
            d.overlayTarget = 1;
            d.overlaySpeed = 1.0f;
            d.overlayColor = 0x000000U;
        } else if (room_eq(room, "room_dw_garden_floradinnencounter")) {
            if (state->sceneFlags & VITA_BORDER_SCENE_CH5_GARDEN_REVEAL) {
                d.overlayTarget = 0;
                d.overlaySpeed = 1.0f / 90.0f;
                d.overlayColor = 0x000000U;
            } else {
                d.overlayTarget = 1;
                d.overlaySpeed = 1.0f;
                d.overlayColor = 0x000000U;
            }
        }
        if (state->sceneFlags & VITA_BORDER_SCENE_CH5_PINK_ALT)
            d.asset = VITA_BORDER_ASSET_DW_PINK_ALT;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH5_CASTLE_GOLD)
            d.asset = VITA_BORDER_ASSET_DW_CASTLE_RIGHT_GOLD;
        else if (state->sceneFlags & VITA_BORDER_SCENE_CH5_CASTLE_TOP)
            d.asset = VITA_BORDER_ASSET_DW_CASTLE_TOP;
        return d;
    }    switch (state->chapter) {
        case 1: d.asset = resolve_ch1(room, state); break;
        case 2: d.asset = resolve_ch2(room, state); break;
        case 3: d.asset = resolve_ch3(room, state); break;
        case 4: d.asset = resolve_ch4(room, state); break;
        case 5: d.asset = resolve_ch5(room, state); break;
        default: d.asset = VITA_BORDER_ASSET_LINE_1080; break;
    }
    return d;
}




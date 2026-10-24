#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vita-probe/source/vita_border_rules.h"

/* Source-priority contract: explicit config > Better Borders > native controller > fallback. */

static void expect_asset(const char* room, VitaBorderRuleState state, const char* expected) {
    VitaBorderDecision d = VitaBorderRules_resolve(room, &state);
    const char* actual = VitaBorderRules_filename(d.asset);
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "room=%s chapter=%d plot=%d flag1324=%d expected=%s actual=%s\n",
                room, state.chapter, state.plot, state.flag1324,
                expected, actual ? actual : "<null>");
        assert(0);
    }
}

int main(void) {
    assert(VitaBorderRules_isMenuRoom("PLACE_MENU") == 1);
    assert(VitaBorderRules_isMenuRoom("PLACE_LOGO") == 1);
    assert(VitaBorderRules_isMenuRoom("room_legend") == 1);
    VitaBorderRuleState s = {0};
    s.chapter = 3; s.plot = 320;
    expect_asset("room_dw_snow_zone", s, "border_dw_tv_black_0.png");
    expect_asset("room_dw_snow_zone_battle", s, "border_dw_blue_stars_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH3_BLUE_STARS;
    expect_asset("room_dw_snow_zone", s, "border_dw_blue_stars_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH3_KNIGHT_NEUTRAL;
    expect_asset("room_dw_snow_zone", s, "border_line_1080_0.png");
    assert(VitaBorderRules_shouldBlankSurround(3,"room_ed",0));
    assert(VitaBorderRules_shouldBlankSurround(4,"room_cc_fountain",0));
    assert(!VitaBorderRules_shouldBlankSurround(3,"room_dw_snow_zone",0));
    memset(&s,0,sizeof(s));

    assert(VitaBorderRules_chooseSource(1, 1, 1) == VITA_BORDER_SOURCE_CUSTOM);
    assert(VitaBorderRules_chooseSource(0, 1, 1) == VITA_BORDER_SOURCE_BETTER);
    assert(VitaBorderRules_chooseSource(0, 0, 1) == VITA_BORDER_SOURCE_CONTROLLER);
    assert(VitaBorderRules_chooseSource(0, 0, 0) == VITA_BORDER_SOURCE_FALLBACK);

    assert(VitaBorderRules_controllerCrossfade(3, 100, 101, 0) == 1);
    assert(VitaBorderRules_controllerCrossfade(4, 200, 201, 0) == 1);
    assert(VitaBorderRules_controllerCrossfade(5, 300, 300, 1) == 1);
    assert(VitaBorderRules_controllerCrossfade(5, 300, 301, 0) == 1);
    assert(VitaBorderRules_controllerCrossfade(3, 100, 100, 0) == 0);

    assert(VitaBorderRules_isMenuRoom("PLACE_MENU") == 1);
    assert(VitaBorderRules_isMenuRoom("CHAPTER_SELECT") == 1);
    assert(VitaBorderRules_isMenuRoom("room_chapter_select") == 1);
    assert(VitaBorderRules_isMenuRoom("room_town_south") == 0);
    assert(VitaBorderRules_isChapter3BoardSwitchOwner("obj_ch3_GSB01") == 1);
    assert(VitaBorderRules_isChapter3BoardSwitchOwner("obj_ch3_audience") == 0);
    assert(VitaBorderRules_isChapter3BoardSwitchOwner("obj_ch3_GSA01G") == 0);
    assert(VitaBorderRules_isChapter3BoardSwitchOwner(NULL) == 0);

    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_intro", 0) == 1);
    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_ralseicupboard", 0) == 1);
    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_meetflowery", 0) == 1);
    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_video", 0) == 1);
    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_floradinnencounter", 0) == 1);
    assert(VitaBorderRules_shouldBlankChapter5Surround(
        5, "room_dw_garden_floradinnencounter", VITA_BORDER_SCENE_CH5_GARDEN_REVEAL) == 0);
    assert(VitaBorderRules_shouldBlankChapter5Surround(5, "room_dw_garden_aqua", 0) == 0);
    assert(VitaBorderRules_shouldBlankChapter5Surround(4, "room_dw_garden_intro", 0) == 0);
    assert(VitaBorderRules_shouldBlankSurround(4, "room_dw_castle_area_1", 0) == 1);
    assert(VitaBorderRules_shouldBlankSurround(5, "room_dw_castle_area_1", 0) == 1);
    assert(VitaBorderRules_shouldBlankSurround(5, "room_dw_castle_town", 0) == 0);
    assert(VitaBorderRules_shouldBlankSurround(5, "PLACE_MENU", 0) == 0);
    assert(VitaBorderRules_shouldBlankSurround(5, "room_dw_garden_intro", 0) == 1);

    s.chapter = 1; s.darkzone = 0;
    expect_asset("room_town_krisyard", s, "border_lw_town_0.png");
    s.darkzone = 1;
    expect_asset("room_field_1", s, "border_dw_castletown_0.png");

    s = (VitaBorderRuleState){ .chapter = 2, .roomIndex = 74, .darkzone = 1, .plot = 100 };
    expect_asset("room_dw_cyber_intro_1", s, "border_dw_cyber_0.png");
    s.roomIndex = 106;
    expect_asset("room_dw_city_prototype_01", s, "border_dw_cyber_0.png");
    s.roomIndex = 107;
    expect_asset("room_dw_city_prototype_02", s, "border_dw_cyber_0.png");
    s.roomIndex = 109;
    expect_asset("room_dw_city_traffic_1", s, "border_dw_city_0.png");
    s.roomIndex = 151;
    expect_asset("room_dw_mansion_entrance", s, "border_dw_mansion_0.png");
    s.roomIndex = -1;
    expect_asset("room_shop_ch2_music", s, "border_dw_cyber_0.png");
    s.plot = 220;
    expect_asset("room_shop_ch2_music", s, "border_dw_castletown_0.png");

    s = (VitaBorderRuleState){ .chapter = 3, .plot = 69, .darkzone = 1 };
    expect_asset("room_dw_backstage", s, "border_dw_tv_black_0.png");
    s.plot = 70;
    expect_asset("room_dw_backstage", s, "border_dw_blue_stars_0.png");
    s.plot = 122;
    expect_asset("room_ch3_gameshowroom", s, "border_dw_tv_black_0.png");
    s.plot = 230;
    expect_asset("room_dw_backstage", s, "border_dw_red_smiles_0.png");
    expect_asset("room_dw_changing_room", s, "border_dw_green_sloppy_0.png");
    expect_asset("room_dw_ranking_z", s, "border_dw_green_sloppy_z_0.png");
    expect_asset("room_dw_puzzlecloset_2", s, "border_dw_tv_blue_0.png");

    s = (VitaBorderRuleState){ .chapter = 4, .plot = 110, .darkzone = 1 };
    expect_asset("room_dw_church_intro1", s, "border_dw_church_a_0.png");
    s.plot = 230;
    expect_asset("room_dw_churchb_savepoint", s, "border_dw_church_b_0.png");
    s.plot = 250;
    expect_asset("room_dw_churchc_finalclimb", s, "border_dw_church_c_0.png");
    expect_asset("room_dw_churchb_rotatingtower", s, "border_dw_church_c_0.png");
    s.plot = 230;
    expect_asset("room_dw_churchc_encounter1", s, "border_dw_church_b_0.png");
    s.plot = 250;    expect_asset("room_dw_churchc_titanclimb1", s, "border_dw_titan_base_0.png");
    s.plot = 300; s.darkzone = 0;
    expect_asset("room_town_mid", s, "border_lw_town_night_0.png");

    s = (VitaBorderRuleState){ .chapter = 5, .plot = 0, .flag1324 = 0, .darkzone = 0 };
    expect_asset("room_town_mid", s, "border_lw_town_morning_0.png");
    s.flag1324 = 2;
    expect_asset("room_town_mid", s, "border_lw_town_sunset_0.png");
    s.flag1324 = 3;
    expect_asset("room_town_mid", s, "border_lw_town_night_0.png");
    expect_asset("room_dw_garden_ralseicupboard", s, "border_dw_garden_0.png");
    expect_asset("room_dw_garden_aqua", s, "border_dw_garden_cliff_0.png");
    expect_asset("room_dw_cliff_seth_miniboss", s, "border_dw_garden_cliff_0.png");
    expect_asset("room_dw_fcastle_foyer", s, "border_dw_castle_right_0.png");
    expect_asset("room_dw_fcastle_blueroom", s, "border_dw_castle_left_0.png");
    expect_asset("room_dw_fcastle_green_checkpoint", s, "border_dw_castle_right_gold_0.png");
    expect_asset("room_dw_fcastle_top_entrance", s, "border_dw_castle_top_0.png");
    expect_asset("room_dw_fcastle_cafe", s, "border_dw_castle_cafe_0.png");
    expect_asset("room_dw_pink_encounter", s, "border_dw_pink_0.png");
    {
        VitaBorderDecision d = VitaBorderRules_resolve("room_dw_garden_meetflowery", &s);
        assert(d.overlayTarget == 1);
        assert(d.overlayColor == 0x000000U);
    }
    {
        VitaBorderDecision d = VitaBorderRules_resolve("room_dw_garden_ralseicupboard", &s);
        assert(d.overlayTarget == 1);
        assert(d.overlayColor == 0x000000U);
    }
    {
        VitaBorderDecision d = VitaBorderRules_resolve("room_dw_garden_floradinnencounter", &s);
        assert(d.overlayTarget == 1);
        s.sceneFlags |= VITA_BORDER_SCENE_CH5_GARDEN_REVEAL;
        d = VitaBorderRules_resolve("room_dw_garden_floradinnencounter", &s);
        assert(d.overlayTarget == 0);
        assert(d.overlaySpeed > 0.0110f && d.overlaySpeed < 0.0112f);
        s.sceneFlags &= ~VITA_BORDER_SCENE_CH5_GARDEN_REVEAL;
    }

    s = (VitaBorderRuleState){ .chapter = 3, .plot = 122, .darkzone = 1, .sceneFlags = VITA_BORDER_SCENE_CH3_BLUE_STARS };
    expect_asset("room_ch3_gameshowroom", s, "border_dw_blue_stars_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH3_RED_SMILES;
    expect_asset("room_ch3_gameshowroom", s, "border_dw_red_smiles_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH3_TV_BLACK;
    expect_asset("room_ch3_gameshowroom", s, "border_dw_tv_black_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH3_WORD;
    expect_asset("room_ch3_gameshowroom", s, "border_dw_word_0.png");

    s = (VitaBorderRuleState){ .chapter = 5, .darkzone = 1, .flag1324 = 3, .sceneFlags = VITA_BORDER_SCENE_CH5_PINK_ALT };
    expect_asset("room_dw_pink_encounter", s, "border_dw_pink_alt_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH5_CASTLE_GOLD;
    expect_asset("room_dw_fcastle_top_entrance", s, "border_dw_castle_right_gold_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH5_CASTLE_TOP;
    expect_asset("room_dw_fcastle_green_checkpoint", s, "border_dw_castle_top_0.png");
    s.sceneFlags = VITA_BORDER_SCENE_CH5_CASTLE_TOP | VITA_BORDER_SCENE_HIDE_WHITE;
    {
        VitaBorderDecision d = VitaBorderRules_resolve("room_dw_fcastle_green_checkpoint", &s);
        assert(d.overlayTarget == 1);
        assert(d.overlayColor == 0xFFFFFFU);
    }
    puts("vita_border_rules_test: PASS");
    return 0;
}







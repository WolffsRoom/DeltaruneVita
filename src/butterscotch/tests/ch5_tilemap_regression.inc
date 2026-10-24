// Exercise registered GML builtins, including invalid IDs and real room tiles.
static int testChapter5Tilemaps(Runner* runner) {
    VMContext* ctx = runner->vmContext;
    BuiltinFunc getFrame = VM_findBuiltin(ctx, "tilemap_get_frame");
    if (!getFrame) { fprintf(stderr, "CH5_TILEMAP_FRAME=FAIL missing builtin\n"); return EXIT_FAILURE; }
    RValue invalid = RValue_makeReal(-999);
    bool ok = RValue_toReal(getFrame(ctx, &invalid, 1)) == -1;
    uint32_t ids[] = {0, 0, 4, 7, 9, 0};
    Background fixture = {0};
    fixture.gms2TileCount = 3;
    fixture.gms2ItemsPerTileCount = 2;
    fixture.gms2FrameLength = 100000;
    fixture.gms2TileIds = ids;
    ok = ok && Runner_tileAnimationFrame(&fixture, 99999, 0) == 0;
    ok = ok && Runner_tileAnimationFrame(&fixture, 100000, 0) == 1;
    ok = ok && Runner_tileAnimationFrame(&fixture, 200000, 1) == 0;
    ok = ok && Runner_tileFrameIndex(&fixture, 1, 0) == 4;
    ok = ok && Runner_tileFrameIndex(&fixture, 1, 1) == 7;
    ok = ok && Runner_tileFrameIndex(&fixture, 2, 1) == 0;
    ok = ok && Runner_tileFrameIndex(&fixture, 3, 0) == 0;
    ok = ok && Runner_tileFrameIndex(&fixture, 1, -1) == 7;
    int checked = 0;
    for (size_t i = 0; i < arrlenu(runner->runtimeLayers); ++i) {
        RuntimeLayer* layer = &runner->runtimeLayers[i];
        for (size_t j = 0; j < arrlenu(layer->elements); ++j) {
            RuntimeLayerElement* el = &layer->elements[j];
            if (el->type != RuntimeLayerElementType_Tilemap || !el->tilemapData) continue;
            RValue id = RValue_makeReal(el->id);
            double frame = RValue_toReal(getFrame(ctx, &id, 1));
            Background* bg = &runner->dataWin->bgnd.backgrounds[el->tilemapData->backgroundIndex];
            ok = ok && frame >= 0 && frame < (bg->gms2ItemsPerTileCount ? bg->gms2ItemsPerTileCount : 1);
            checked++;
        }
    }
    fprintf(stderr, "CH5_TILEMAP_FRAME=%s maps=%d\n", ok && checked ? "pass" : "FAIL", checked);
    return ok && checked ? EXIT_SUCCESS : EXIT_FAILURE;
}

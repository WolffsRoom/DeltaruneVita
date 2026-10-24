#!/usr/bin/env python3
"""Generate compact localized loading-label atlases for the Vita VPK."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "frames" / "Lang"
OUT_MINIGAME = ROOT / "data" / "frames" / "LangMiniGame"
LATIN_FONT = ROOT / "data" / "frames" / "undertale.otf"

# Rows 0-5 map to LOADING, textures, GENERATING, UPDATING, game files and
# shaders. Row 6 contains two compact 160x64 localized progress labels (TEXTURES and
# SHADERS). Row 7 keeps the fixed-width 0-9/ strip used to compose X / Y and
# reserves its final 160 pixels for native-size 80x32 PLAY/STOP labels used only
# by the Generating minigame footer.
LABELS = {
    # state, detail, generating, updating, game files, shaders, legacy ETA, PLAY, STOP
    "English": ("LOADING", "textures", "GENERATING", "UPDATING", "game files", "shaders", "ESTIMATED TIME", "PLAY", "STOP"),
    "Portuguese-BR": ("CARREGANDO", "texturas", "GERANDO", "ATUALIZANDO", "arquivos do jogo", "shaders", "TEMPO ESTIMADO", "JOGAR", "PARAR"),
    "Spanish": ("CARGANDO", "texturas", "GENERANDO", "ACTUALIZANDO", "archivos del juego", "shaders", "TIEMPO ESTIMADO", "JUGAR", "PARAR"),
    "German": ("LADEN", "texturen", "ERZEUGEN", "AKTUALISIERUNG", "spieldateien", "shaders", "GESCHÄTZTE ZEIT", "SPIELEN", "STOPP"),
    "French": ("CHARGEMENT", "textures", "GÉNÉRATION", "MISE À JOUR", "fichiers du jeu", "shaders", "TEMPS ESTIMÉ", "JOUER", "ARRÊTER"),
    "Italian": ("CARICAMENTO", "texture", "GENERAZIONE", "AGGIORNAMENTO", "file di gioco", "shaders", "TEMPO STIMATO", "GIOCA", "FERMA"),
    "Turkish": ("YÜKLENİYOR", "dokular", "OLUŞTURULUYOR", "GÜNCELLENİYOR", "oyun dosyaları", "shaders", "TAHMİNİ SÜRE", "OYNA", "DUR"),
    "Russian": ("ЗАГРУЗКА", "текстуры", "СОЗДАНИЕ", "ОБНОВЛЕНИЕ", "файлы игры", "shaders", "ОСТАЛОСЬ ВРЕМЕНИ", "ИГРАТЬ", "СТОП"),
    "Japanese": ("読み込み中", "テクスチャ", "生成中", "更新中", "ゲームファイル", "shaders", "残り時間", "プレイ", "停止"),
}

# Compact labels used only by the optional Generating Textures mini-game.
# The texture is 512x64 with four 128x64 cells: GRAZE, COMBO, CLEAR and NO HIT.
MINIGAME_LABELS = {
    "English": ("GRAZE", "COMBO", "CLEAR!", "NO HIT!"),
    "Portuguese-BR": ("RASANTE", "COMBO", "LIMPO!", "SEM DANO!"),
    "Spanish": ("ROCE", "COMBO", "SUPERADO!", "SIN DAÑO!"),
    "German": ("STREIFER", "KOMBO", "GESCHAFFT!", "KEIN TREFFER!"),
    "French": ("FRÔLÉ", "COMBO", "RÉUSSI!", "SANS DÉGÂT!"),
    "Italian": ("SFIORO", "COMBO", "FATTO!", "NESSUN DANNO!"),
    "Turkish": ("SIYIRMA", "KOMBO", "TAMAM!", "HASARSIZ!"),
    "Russian": ("КАСАНИЕ", "КОМБО", "ГОТОВО!", "БЕЗ УРОНА!"),
    "Japanese": ("かすり", "コンボ", "クリア!", "ノーダメージ!"),
}


def font_path(language: str) -> Path:
    if language == "Japanese":
        return Path(r"C:\Windows\Fonts\msgothic.ttc")
    if language == "Russian":
        return Path(r"C:\Windows\Fonts\arial.ttf")
    return LATIN_FONT


def fitted_font(path: Path, text: str, wanted: int, max_width: int):
    size = wanted
    while size >= 12:
        font = ImageFont.truetype(str(path), size)
        box = font.getbbox(text)
        if box[2] - box[0] <= max_width:
            return font
        size -= 1
    return ImageFont.truetype(str(path), 12)


def draw_centered(draw: ImageDraw.ImageDraw, language: str, text: str,
                  row: int, wanted_size: int):
    path = font_path(language)
    if not path.exists():
        path = LATIN_FONT
    font = fitted_font(path, text, wanted_size, 480)
    box = draw.textbbox((0, 0), text, font=font, stroke_width=0)
    width, height = box[2] - box[0], box[3] - box[1]
    x = (512 - width) // 2 - box[0]
    y = row * 64 + (64 - height) // 2 - box[1]
    draw.text((x, y), text, font=font, fill=(255, 255, 255, 255))


def draw_progress_label(draw: ImageDraw.ImageDraw, language: str, text: str, cell: int) -> None:
    """Draw a localized uppercase label into one 160x64 cell of row 6."""
    path = font_path(language)
    if not path.exists():
        path = LATIN_FONT
    text = text.upper()
    cell_width = 160
    font = fitted_font(path, text, 24, cell_width - 8)
    box = draw.textbbox((0, 0), text, font=font, stroke_width=0)
    width, height = box[2] - box[0], box[3] - box[1]
    x = cell * cell_width + (cell_width - width) // 2 - box[0]
    y = 6 * 64 + (64 - height) // 2 - box[1]
    draw.text((x, y), text, font=font, fill=(255, 255, 255, 255))


def draw_progress_digits(atlas: Image.Image) -> None:
    """Put 0-9 and '/' in 32x64 cells on the final atlas row."""
    draw = ImageDraw.Draw(atlas)
    font = ImageFont.truetype(str(LATIN_FONT), 30)
    for index, character in enumerate("0123456789/"):
        box = draw.textbbox((0, 0), character, font=font, stroke_width=0)
        width, height = box[2] - box[0], box[3] - box[1]
        x = index * 32 + (32 - width) // 2 - box[0]
        y = 7 * 64 + (64 - height) // 2 - box[1]
        draw.text((x, y), character, font=font, fill=(255, 255, 255, 255))


def draw_action_label(draw: ImageDraw.ImageDraw, language: str, text: str, cell: int) -> None:
    """Draw an 80x32 native-size PLAY/STOP label in row 7's unused tail."""
    path = font_path(language)
    if not path.exists():
        path = LATIN_FONT
    x0 = 352 + cell * 80
    y0 = 448
    font = fitted_font(path, text.upper(), 18, 74)
    box = draw.textbbox((0, 0), text.upper(), font=font, stroke_width=0)
    width, height = box[2] - box[0], box[3] - box[1]
    x = x0 + (80 - width) // 2 - box[0]
    y = y0 + (32 - height) // 2 - box[1]
    draw.text((x, y), text.upper(), font=font, fill=(255, 255, 255, 255))



def draw_minigame_label(draw: ImageDraw.ImageDraw, language: str, text: str, cell: int) -> None:
    path = font_path(language)
    if not path.exists():
        path = LATIN_FONT
    x0 = cell * 128
    text = text.upper() if language != "Japanese" else text
    font = fitted_font(path, text, 24, 120)
    box = draw.textbbox((0, 0), text, font=font, stroke_width=0)
    width, height = box[2] - box[0], box[3] - box[1]
    x = x0 + (128 - width) // 2 - box[0]
    y = (64 - height) // 2 - box[1]
    draw.text((x, y), text, font=font, fill=(255, 255, 255, 255))

def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    OUT_MINIGAME.mkdir(parents=True, exist_ok=True)
    for language, labels in LABELS.items():
        atlas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        draw = ImageDraw.Draw(atlas)
        draw_centered(draw, language, labels[0], 0, 32)
        draw_centered(draw, language, labels[1], 1, 20)
        draw_centered(draw, language, labels[2], 2, 32)
        draw_centered(draw, language, labels[3], 3, 32)
        draw_centered(draw, language, labels[4], 4, 20)
        draw_centered(draw, language, labels[5], 5, 20)
        draw_progress_label(draw, language, labels[1], 0)
        draw_progress_label(draw, language, labels[5], 1)
        draw_progress_digits(atlas)
        draw_action_label(draw, language, labels[7], 0)
        draw_action_label(draw, language, labels[8], 1)
        # Keep the labels crisp under GL_NEAREST and avoid semi-transparent
        # antialias fringes around the white pixel font.
        alpha = atlas.getchannel("A").point(lambda value: 255 if value >= 128 else 0)
        atlas.putalpha(alpha)
        target = OUT / f"{language}.png"
        atlas.save(target, optimize=True)
        print(f"generated {target.relative_to(ROOT)}")

        mini = Image.new("RGBA", (512, 64), (0, 0, 0, 0))
        mini_draw = ImageDraw.Draw(mini)
        for cell, text in enumerate(MINIGAME_LABELS[language]):
            draw_minigame_label(mini_draw, language, text, cell)
        mini_alpha = mini.getchannel("A").point(lambda value: 255 if value >= 128 else 0)
        mini.putalpha(mini_alpha)
        mini_target = OUT_MINIGAME / f"{language}.png"
        mini.save(mini_target, optimize=True)
        print(f"generated {mini_target.relative_to(ROOT)}")


if __name__ == "__main__":
    main()

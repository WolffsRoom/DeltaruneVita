# -*- coding: utf-8 -*-
"""DELTARUNE PS Vita data patcher by WolffsRoom."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.request
import urllib.parse
import zipfile
from pathlib import Path, PurePosixPath

import bsdiff4

import console_ui as ui
import pvr_pipeline

VERSION = "0.73"
LANG_MANIFEST_URL = "https://wolffsroom.github.io/DeltaruneVita/language-packs/language_packs.json"
LANGUAGES = [
    ("English", "EN"), ("Portuguese (Brasil)", "PT"), ("Espanol", "ES"),
    ("Francais", "FR"), ("Portuguese (Portugal)", "PTPT"), ("Italian", "IT"),
    ("Russkiy", "RU"), ("Japanese", "JP"),
]

TR = {
    "EN": dict(note="Depending on the language you selected, a translation for the game may be available for you to install.",
        instruction="Copy an original, unmodified DELTARUNE Steam installation into SteamFiles.",
        output="The output will be created in VitaFiles\\deltarune.", found="Steam installation found",
        missing="No valid DELTARUNE Steam installation was found in SteamFiles.", confirm="Start generating VitaFiles?",
        yes="Y", cancelled="Operation cancelled.", verify="Verifying Steam files", incompatible="Incompatible or modified Steam file",
        generating="Generating Vita data", checkfail="Output verification failed", success="SUCCESS! All files were generated and verified.",
        copy="Copy the 'deltarune' folder to ux0:data/ on the PS Vita.", error="ERROR", close="Press ENTER to close...",
        extra_lang_prompt="Do you want an additional language (besides English and Japonese)?",
        extra_lang_ptbr="Portuguese (PTBR)", extra_lang_none="None", invalid_option="Invalid option.",
        mod_trying="Trying", mod_download_warn="Warning: Could not automatically download translation mod:",
        mod_download_manual_hint="You can still manually download it and place it in deltarunevita/mods.",
        locked_folder="Could not remove the previous output folder. Close any program using it (Explorer, antivirus) and try again",
        scr_language="SELECT LANGUAGE", scr_prepare="STEAM INSTALLATION", scr_generate="GENERATING VITA DATA",
        scr_mod="TRANSLATION MOD", scr_pvr="TEXTURE OPTIMIZATION", scr_done="DONE",
        verifying_label="Verifying Steam files", generating_label="Writing Vita files",
        downloading_label="Downloading", extracting_label="Extracting mod",
        pvr_scanning="Optimizing mod textures", pvr_no_tool="Texture optimizer not found next to the patcher - the mod will still work, just using the slower on-Vita fallback for its pages.",
        pvr_chapter="Chapter {ch}", pvr_summary="{reused} pages reused from the base game, {generated} re-encoded for the translation",
        pvr_done="Texture optimization complete", pvr_skip_no_pages="no data.win found for this chapter, skipped"),
    "PT": dict(note="De acordo com a lingua selecionada, caso haja traducao disponivel, ela podera ser instalada.",
        instruction="Copie uma instalacao original e sem modificacoes do DELTARUNE da Steam para SteamFiles.",
        output="A saida sera criada em VitaFiles\\deltarune.", found="Instalacao Steam encontrada",
        missing="Nenhuma instalacao valida do DELTARUNE foi encontrada em SteamFiles.", confirm="Iniciar a geracao do VitaFiles?",
        yes="S", cancelled="Operacao cancelada.", verify="Verificando arquivos da Steam", incompatible="Arquivo Steam incompativel ou modificado",
        generating="Gerando dados do Vita", checkfail="Falha ao verificar a saida", success="SUCESSO! Todos os arquivos foram gerados e verificados.",
        copy="Copie a pasta 'deltarune' para ux0:data/ no PS Vita.", error="ERRO", close="Pressione ENTER para fechar...",
        extra_lang_prompt="Deseja algum idioma adicional (alem de ingles e japones)?",
        extra_lang_ptbr="Portugues (PTBR)", extra_lang_none="Nenhum", invalid_option="Opcao invalida.",
        mod_trying="Tentando", mod_download_warn="Aviso: Nao foi possivel baixar automaticamente o mod de traducao:",
        mod_download_manual_hint="Voce ainda pode baixa-lo manualmente e coloca-lo em deltarunevita/mods.",
        locked_folder="Nao foi possivel remover a pasta de saida anterior. Feche qualquer programa que a esteja usando (Explorer, antivirus) e tente novamente",
        scr_language="SELECIONE O IDIOMA", scr_prepare="INSTALACAO STEAM", scr_generate="GERANDO DADOS DO VITA",
        scr_mod="MOD DE TRADUCAO", scr_pvr="OTIMIZACAO DE TEXTURAS", scr_done="CONCLUIDO",
        verifying_label="Verificando arquivos da Steam", generating_label="Gravando arquivos do Vita",
        downloading_label="Baixando", extracting_label="Extraindo mod",
        pvr_scanning="Otimizando texturas do mod", pvr_no_tool="Otimizador de textura nao encontrado ao lado do patcher - o mod ainda vai funcionar, so vai usar o fallback mais lento no proprio Vita pra essas paginas.",
        pvr_chapter="Capitulo {ch}", pvr_summary="{reused} paginas reaproveitadas do jogo base, {generated} recodificadas pra traducao",
        pvr_done="Otimizacao de texturas concluida", pvr_skip_no_pages="nenhum data.win encontrado pra esse capitulo, pulado"),
    "ES": dict(note="Segun el idioma seleccionado, si hay una traduccion disponible, podras instalarla.",
        instruction="Copia una instalacion original de DELTARUNE de Steam dentro de SteamFiles.", output="La salida se creara en VitaFiles\\deltarune.",
        found="Instalacion de Steam encontrada", missing="No se encontro una instalacion valida de DELTARUNE en SteamFiles.",
        confirm="Iniciar la generacion de VitaFiles?", yes="S", cancelled="Operacion cancelada.", verify="Verificando archivos de Steam",
        incompatible="Archivo de Steam incompatible o modificado", generating="Generando datos de Vita", checkfail="Error al verificar la salida",
        success="EXITO! Todos los archivos fueron generados y verificados.", copy="Copia la carpeta 'deltarune' a ux0:data/ en PS Vita.", error="ERROR", close="Pulsa ENTER para cerrar...",
        extra_lang_prompt="Quieres un idioma adicional (ademas de ingles y japones)?",
        extra_lang_ptbr="Portugues (PTBR)", extra_lang_none="Ninguno", invalid_option="Opcion invalida.",
        mod_trying="Intentando", mod_download_warn="Aviso: no se pudo descargar automaticamente el mod de traduccion:",
        mod_download_manual_hint="Aun puedes descargarlo manualmente y colocarlo en deltarunevita/mods.",
        locked_folder="No se pudo eliminar la carpeta de salida anterior. Cierra cualquier programa que la este usando (Explorador, antivirus) e intenta de nuevo"),
    "FR": dict(note="Selon la langue selectionnee, une traduction pourra etre installee si elle est disponible.",
        instruction="Copiez une installation Steam originale de DELTARUNE dans SteamFiles.", output="La sortie sera creee dans VitaFiles\\deltarune.",
        found="Installation Steam trouvee", missing="Aucune installation DELTARUNE valide trouvee dans SteamFiles.", confirm="Commencer la creation de VitaFiles ?",
        yes="O", cancelled="Operation annulee.", verify="Verification des fichiers Steam", incompatible="Fichier Steam incompatible ou modifie",
        generating="Creation des donnees Vita", checkfail="Echec de verification de la sortie", success="SUCCES ! Tous les fichiers ont ete crees et verifies.",
        copy="Copiez le dossier 'deltarune' vers ux0:data/ sur PS Vita.", error="ERREUR", close="Appuyez sur ENTREE pour fermer...",
        extra_lang_prompt="Voulez-vous une langue supplementaire (en plus de l'anglais et du japonais) ?",
        extra_lang_ptbr="Portugais (PTBR)", extra_lang_none="Aucune", invalid_option="Option invalide.",
        mod_trying="Tentative", mod_download_warn="Avertissement : impossible de telecharger automatiquement le mod de traduction :",
        mod_download_manual_hint="Vous pouvez toujours le telecharger manuellement et le placer dans deltarunevita/mods.",
        locked_folder="Impossible de supprimer l'ancien dossier de sortie. Fermez tout programme qui l'utilise (Explorateur, antivirus) et reessayez"),
    "PTPT": dict(note="De acordo com a lingua selecionada, caso haja traducao disponivel, podera ser instalada.",
        instruction="Copie uma instalacao Steam original do DELTARUNE para SteamFiles.", output="A saida sera criada em VitaFiles\\deltarune.",
        found="Instalacao Steam encontrada", missing="Nao foi encontrada uma instalacao DELTARUNE valida em SteamFiles.", confirm="Iniciar a criacao de VitaFiles?",
        yes="S", cancelled="Operacao cancelada.", verify="A verificar ficheiros Steam", incompatible="Ficheiro Steam incompativel ou modificado",
        generating="A criar dados Vita", checkfail="Falha ao verificar a saida", success="SUCESSO! Todos os ficheiros foram criados e verificados.",
        copy="Copie a pasta 'deltarune' para ux0:data/ na PS Vita.", error="ERRO", close="Prima ENTER para fechar...",
        extra_lang_prompt="Deseja algum idioma adicional (alem de ingles e japones)?",
        extra_lang_ptbr="Portugues (PTBR)", extra_lang_none="Nenhum", invalid_option="Opcao invalida.",
        mod_trying="A tentar", mod_download_warn="Aviso: Nao foi possivel transferir automaticamente o mod de traducao:",
        mod_download_manual_hint="Ainda pode transferi-lo manualmente e coloca-lo em deltarunevita/mods.",
        locked_folder="Nao foi possivel remover a pasta de saida anterior. Feche qualquer programa que a esteja a usar (Explorador, antivirus) e tente novamente"),
    "IT": dict(note="In base alla lingua selezionata, se disponibile, potrai installare una traduzione.",
        instruction="Copia un'installazione Steam originale di DELTARUNE in SteamFiles.", output="I file verranno creati in VitaFiles\\deltarune.",
        found="Installazione Steam trovata", missing="Nessuna installazione DELTARUNE valida trovata in SteamFiles.", confirm="Avviare la generazione di VitaFiles?",
        yes="S", cancelled="Operazione annullata.", verify="Verifica dei file Steam", incompatible="File Steam incompatibile o modificato",
        generating="Generazione dati Vita", checkfail="Verifica dei file generati fallita", success="SUCCESSO! Tutti i file sono stati generati e verificati.",
        copy="Copia la cartella 'deltarune' in ux0:data/ sulla PS Vita.", error="ERRORE", close="Premi INVIO per chiudere...",
        extra_lang_prompt="Vuoi una lingua aggiuntiva (oltre a inglese e japonese)?",
        extra_lang_ptbr="Portoghese (PTBR)", extra_lang_none="Nessuna", invalid_option="Opzione non valida.",
        mod_trying="Provo", mod_download_warn="Attenzione: impossibile scaricare automaticamente la mod di traduzione:",
        mod_download_manual_hint="Puoi comunque scaricarla manualmente e metterla in deltarunevita/mods.",
        locked_folder="Impossibile rimuovere la cartella di output precedente. Chiudi qualsiasi programma la stia usando (Esplora file, antivirus) e riprova"),
    "RU": dict(note="V zavisimosti ot vybrannogo yazyka, esli perevod dostupen, vy smozhete ego ustanovit.", instruction="Skopiruyte originalnuyu Steam-versiyu DELTARUNE v SteamFiles.",
        output="Rezultat budet sozdan v VitaFiles\\deltarune.", found="Steam-versiya naydena", missing="V SteamFiles net podhodyashchey versii DELTARUNE.",
        confirm="Nachat sozdanie VitaFiles?", yes="Y", cancelled="Operatsiya otmenena.", verify="Proverka faylov Steam", incompatible="Fayl Steam izmenyon ili nesovmestim",
        generating="Sozdanie dannyh Vita", checkfail="Oshibka proverki rezultata", success="USPEH! Vse fayly sozdany i provereny.",
        copy="Skopiruyte papku 'deltarune' v ux0:data/ na PS Vita.", error="OSHIBKA", close="Nazhmite ENTER dlya vyhoda...",
        extra_lang_prompt="Khotite dopolnitelny yazyk (krome angliyskogo i yaponskogo yazykov.)?",
        extra_lang_ptbr="Portugalskiy (PTBR)", extra_lang_none="Net", invalid_option="Nevernaya optsiya.",
        mod_trying="Probuyu", mod_download_warn="Preduprezhdenie: ne udalos avtomaticheski zagruzit mod perevoda:",
        mod_download_manual_hint="Vy vse ravno mozhete zagruzit ego vruchnuyu i pomestit v deltarunevita/mods.",
        locked_folder="Ne udalos udalit predydushchuyu papku vyvoda. Zakroyte lyubuyu programmu, ispolzuyushchuyu eyo (Provodnik, antivirus), i poprobuyte snova"),
    "JP": dict(note="Sentaku shita gengo ni yotte, hon'yaku ga riyou kanou na baai, insutooru dekimasu.",
        instruction="Steam no seiki DELTARUNE foruda wo SteamFiles ni kopii shite kudasai.", output="VitaFiles\\deltarune ni sakusei shimasu.",
        found="Steam foruda ga mitsukarimashita", missing="SteamFiles ni DELTARUNE ga arimasen.", confirm="VitaFiles wo sakusei shimasu ka?",
        yes="Y", cancelled="Kyanseru shimashita.", verify="Steam fairu kakunin-chu", incompatible="Steam fairu ga chigaimasu",
        generating="Vita deeta sakusei-chu", checkfail="Shutsuryoku kakunin ni shippai", success="SEIKOU! Subete no fairu wo sakusei kakunin shimashita.",
        copy="'deltarune' foruda wo PS Vita no ux0:data/ ni kopii shite kudasai.", error="ERAA", close="ENTER de shuuryou...",
        extra_lang_prompt="Tsuika no gengo wo kibou shimasu ka? (Eigo matawa nihongo o nozoku)",
        extra_lang_ptbr="Porutogarugo (PTBR)", extra_lang_none="Nashi", invalid_option="Muko na sentaku desu.",
        mod_trying="Tameshite imasu", mod_download_warn="Keikoku: Hon'yaku mod no jidou daunrodo ni shippai shimashita:",
        mod_download_manual_hint="Techisuto de daunrodo shite deltarunevita/mods ni oite kudasai.",
        locked_folder="Mae no shutsuryoku foruda wo sakujo dekimasen deshita. Sore wo shiyou-chuu no puroguramu (Ekusupuroora, uirusu taisaku sofuto) wo tojite, mou ichido tameshite kudasai"),
}

EXTRA_UI = {
    "EN": {
        "optional_languages": "Optional languages available (you can select more than one):",
        "toggle_languages": "Enter numbers separated by commas to toggle, or press ENTER to continue.",
        "original_language_note": "If no optional language is selected, the game will remain in its original language: English and/or Japanese.",
        "language_suggestion": 'Hmm, I noticed you selected "{name}". Would you like me to include this translation?',
        "official_game_data": "OFFICIAL GAME DATA",
        "supported_steam": "Supported Steam build: DELTARUNE v0.0.253",
        "vita_output": "Vita output: ux0:data/deltarune",
        "validation_note": "The patcher validates source and generated files before completion.",
        "language_pack": "LANGUAGE PACK - {name}",
        "language_installed": "{name} installed",
        "downloading_translation": "Downloading translation from {url}",
        "extracting_translation": "Extracting translation",
    },
    "PT": {
        "optional_languages": "Idiomas opcionais disponíveis (pode selecionar mais de um):",
        "toggle_languages": "Digite os números separados por vírgula para alternar, ou pressione ENTER para continuar.",
        "original_language_note": "Se nenhum idioma opcional for selecionado, o jogo ficará no idioma original: Inglês e/ou Japonês.",
        "language_suggestion": 'Hmm, notei que você selecionou "{name}". Deseja incluir essa tradução?',
        "official_game_data": "DADOS OFICIAIS DO JOGO",
        "supported_steam": "Versão Steam suportada: DELTARUNE v0.0.253",
        "vita_output": "Saída no Vita: ux0:data/deltarune",
        "validation_note": "O patcher valida os arquivos de origem e os arquivos gerados antes de concluir.",
        "language_pack": "PACOTE DE IDIOMA - {name}",
        "language_installed": "{name} instalado",
        "downloading_translation": "Baixando tradução de {url}",
        "extracting_translation": "Extraindo tradução",
    },
    "ES": {
        "optional_languages": "Idiomas opcionales disponibles (puede seleccionar más de uno):",
        "toggle_languages": "Escriba los números separados por comas para alternar, o pulse ENTER para continuar.",
        "original_language_note": "Si no selecciona ningún idioma opcional, el juego permanecerá en su idioma original: Inglés y/o Japonés.",
        "language_suggestion": 'Hmm, noté que seleccionaste "{name}". ¿Quieres incluir esta traducción?',
        "official_game_data": "DATOS OFICIALES DEL JUEGO",
        "supported_steam": "Versión de Steam compatible: DELTARUNE v0.0.253",
        "vita_output": "Salida en Vita: ux0:data/deltarune",
        "validation_note": "El patcher valida los archivos de origen y generados antes de finalizar.",
        "language_pack": "PAQUETE DE IDIOMA - {name}",
        "language_installed": "{name} instalado",
        "downloading_translation": "Descargando traducción desde {url}",
        "extracting_translation": "Extrayendo traducción",
    },
    "FR": {
        "optional_languages": "Langues optionnelles disponibles (vous pouvez en sélectionner plusieurs) :",
        "toggle_languages": "Entrez les numéros séparés par des virgules pour basculer, ou appuyez sur ENTRÉE pour continuer.",
        "original_language_note": "Si aucune langue optionnelle n'est sélectionnée, le jeu restera dans sa langue d'origine : anglais et/ou japonais.",
        "language_suggestion": 'Hmm, vous avez sélectionné "{name}". Voulez-vous inclure cette traduction ?',
        "official_game_data": "DONNÉES OFFICIELLES DU JEU",
        "supported_steam": "Version Steam prise en charge : DELTARUNE v0.0.253",
        "vita_output": "Sortie Vita : ux0:data/deltarune",
        "validation_note": "Le patcher valide les fichiers source et générés avant de terminer.",
        "language_pack": "PACK DE LANGUE - {name}",
        "language_installed": "{name} installé",
        "downloading_translation": "Téléchargement de la traduction depuis {url}",
        "extracting_translation": "Extraction de la traduction",
    },
    "PTPT": {
        "optional_languages": "Idiomas opcionais disponíveis (pode selecionar mais do que um):",
        "toggle_languages": "Introduza os números separados por vírgulas para alternar, ou prima ENTER para continuar.",
        "original_language_note": "Se nenhum idioma opcional for selecionado, o jogo ficará no idioma original: Inglês e/ou Japonês.",
        "language_suggestion": 'Hmm, reparei que selecionou "{name}". Deseja incluir esta tradução?',
        "official_game_data": "DADOS OFICIAIS DO JOGO",
        "supported_steam": "Versão Steam suportada: DELTARUNE v0.0.253",
        "vita_output": "Saída na Vita: ux0:data/deltarune",
        "validation_note": "O patcher valida os ficheiros de origem e os ficheiros gerados antes de concluir.",
        "language_pack": "PACOTE DE IDIOMA - {name}",
        "language_installed": "{name} instalado",
        "downloading_translation": "A transferir tradução de {url}",
        "extracting_translation": "A extrair tradução",
    },
    "IT": {
        "optional_languages": "Lingue opzionali disponibili (puoi selezionarne più di una):",
        "toggle_languages": "Inserisci i numeri separati da virgole per selezionare/deselezionare, oppure premi INVIO per continuare.",
        "original_language_note": "Se non selezioni alcuna lingua opzionale, il gioco resterà nella lingua originale: inglese e/o giapponese.",
        "language_suggestion": 'Hmm, hai selezionato "{name}". Vuoi includere questa traduzione?',
        "official_game_data": "DATI UFFICIALI DEL GIOCO",
        "supported_steam": "Versione Steam supportata: DELTARUNE v0.0.253",
        "vita_output": "Output Vita: ux0:data/deltarune",
        "validation_note": "Il patcher convalida i file sorgente e generati prima di terminare.",
        "language_pack": "PACCHETTO LINGUA - {name}",
        "language_installed": "{name} installato",
        "downloading_translation": "Download della traduzione da {url}",
        "extracting_translation": "Estrazione della traduzione",
    },
    "RU": {
        "optional_languages": "Dostupnye dopolnitelnye yazyki (mozhno vybrat neskolko):",
        "toggle_languages": "Vvedite nomera cherez zapyatuyu dlya pereklyucheniya ili nazhmite ENTER dlya prodolzheniya.",
        "original_language_note": "Esli dopolnitelny yazyk ne vybran, igra ostanetsya na originalnom yazyke: angliyskom i/ili yaponskom.",
        "language_suggestion": 'Hmm, vy vybrali "{name}". Dobavit etot perevod?',
        "official_game_data": "OFITSIALNYE DANNYE IGRY",
        "supported_steam": "Podderzhivaemaya versiya Steam: DELTARUNE v0.0.253",
        "vita_output": "Vyvod Vita: ux0:data/deltarune",
        "validation_note": "Patcher proverit iskhodnye i sozdannye fayly pered zaversheniem.",
        "language_pack": "YAZYKOVOY PAKET - {name}",
        "language_installed": "{name} ustanovlen",
        "downloading_translation": "Zagruzka perevoda iz {url}",
        "extracting_translation": "Raspakovka perevoda",
    },
    "JP": {
        "optional_languages": "Tsuika gengo (fukusuu sentaku kanou):",
        "toggle_languages": "Bangou wo kanma kugiri de nyuuryoku shite kirikae, matawa ENTER de tsuzukemasu.",
        "original_language_note": "Tsuika gengo wo sentaku shinai baai, geemu wa genbun no Eigo matawa Nihongo no mama desu.",
        "language_suggestion": 'Hmm, "{name}" wo sentaku shimashita. Kono hon-yaku wo tsuika shimasu ka?',
        "official_game_data": "SEISHIKI GAME DATA",
        "supported_steam": "Taiou Steam version: DELTARUNE v0.0.253",
        "vita_output": "Vita shutsuryoku: ux0:data/deltarune",
        "validation_note": "Patcher wa kanryou mae ni moto fairu to sakusei fairu wo kakunin shimasu.",
        "language_pack": "GENGO PACK - {name}",
        "language_installed": "{name} wo install shimashita",
        "downloading_translation": "Hon-yaku wo download-chuu: {url}",
        "extracting_translation": "Hon-yaku wo tenkai-chuu",
    },
}

def TX(lang: str, key: str) -> str:
    return EXTRA_UI.get(lang, EXTRA_UI["EN"]).get(key, EXTRA_UI["EN"].get(key, key))


def T(text: dict, key: str) -> str:
    """Look up a UI string, falling back to English for languages that don't
    have a translation yet (new keys added after the original 8 blocks)."""
    return text.get(key, TR["EN"].get(key, key))


def app_dir() -> Path:
    return Path(sys.executable if getattr(sys, "frozen", False) else __file__).resolve().parent


def resource(relative: str) -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent)) / relative


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""): h.update(block)
    return h.hexdigest()


def safe_path(value: str) -> Path:
    posix = PurePosixPath(value)
    if posix.is_absolute() or ".." in posix.parts: raise ValueError("Invalid path in patch manifest")
    return Path(*posix.parts)


def find_steam(text: dict) -> Path:
    base = app_dir() / "SteamFiles"
    for candidate in (base / "DELTARUNE", base):
        if (candidate / "DELTARUNE.exe").is_file() and (candidate / "data.win").is_file(): return candidate
    raise ValueError(text["missing"])


def verify_steam(steam: Path, manifest: dict, text: dict) -> None:
    sources = manifest.get("required_sources", {})
    label = T(text, "verifying_label")
    for index, (relative, expected) in enumerate(sources.items(), 1):
        if safe_path(relative).suffix.lower() == ".ini":
            continue
        source = steam / safe_path(relative)
        if not source.is_file() or source.stat().st_size != expected["size"] or sha256(source) != expected["sha256"]:
            raise ValueError(f"{text['incompatible']}: {relative}")
        ui.progress_bar(index, len(sources), label)


def generate(steam: Path, manifest: dict, text: dict) -> Path:
    destination = app_dir() / "VitaFiles" / manifest["output_folder"]
    temporary = destination.with_name(destination.name + ".tmp")
    shutil.rmtree(temporary, ignore_errors=True)
    temporary.mkdir(parents=True)
    files = manifest["files"]
    label = T(text, "generating_label")
    try:
        for index, record in enumerate(files, 1):
            source_name = record.get("source")
            ini_name = source_name or record.get("output", "")
            if safe_path(ini_name).suffix.lower() == ".ini":
                continue
            output = temporary / safe_path(record["output"])
            output.parent.mkdir(parents=True, exist_ok=True)
            source = steam / safe_path(source_name) if source_name else None
            if record["mode"] == "copy":
                if source is None: raise ValueError(record["output"])
                shutil.copyfile(source, output)
            elif record["mode"] == "embed":
                shutil.copyfile(resource("patch_data/embedded") / safe_path(record["output"]), output)
            else:
                if source is None: raise ValueError(record["output"])
                bsdiff4.file_patch(str(source), str(output), str(resource("patch_data/patches") / record["patch"]))
            if output.stat().st_size != record["size"] or sha256(output) != record["sha256"]:
                raise ValueError(f"{text['checkfail']}: {record['output']}")
            ui.progress_bar(index, len(files), label)
        shutil.rmtree(destination, ignore_errors=True)
        if destination.exists():
            # A locked file (antivirus scanning a just-written asset, Explorer preview, etc.)
            # can leave a leftover non-empty folder; os.replace() then fails with a raw
            # WinError 5 instead of a clear message. Retry once, then fail clearly.
            time.sleep(1.0)
            shutil.rmtree(destination, ignore_errors=True)
        if destination.exists():
            raise ValueError(f"{text['locked_folder']}: {destination}")
        os.replace(temporary, destination)
        validate_and_describe_output(destination, manifest)
        return destination
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def validate_and_describe_output(destination: Path, manifest: dict) -> None:
    """Validate streamable audio and record PC-side preparation details."""
    ogg_files = list(destination.rglob("*.ogg"))
    for audio in ogg_files:
        with audio.open("rb") as stream:
            if stream.read(4) != b"OggS":
                raise ValueError(f"Invalid OGG stream: {audio.relative_to(destination)}")
    borders = list((destination / "deltarunevita" / "borders").glob("*.png"))
    report = {
        "patcher_version": VERSION,
        "port_version": manifest.get("port_version"),
        "steam_version": "v0.0.253",
        "validated_ogg_files": len(ogg_files),
        "prepared_console_borders": len(borders),
        "console_border_size": "960x544",
        "data_optimizations": manifest.get("optimizations", {}),
        "audio_layout": {
            "external_music": "deltarunevita/music (streamed OGG)",
            "chapter_audio": "external OGG plus GameMaker data.win/audiogroup1.dat",
            "sample_rate_conversion": "disabled to preserve timing and quality",
        },
    }
    report_path = destination / "deltarunevita" / "patcher-report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def draw_header(subtitle: str = "") -> None:
    ui.screen("DELTARUNE", subtitle or "WELCOME")


def choose_language() -> str:
    draw_header()
    ui.section("PATCHER INTERFACE LANGUAGE")
    ui.type_out("  Choose the language used by this patcher interface.", delay=0.004)
    print()
    for index, (name, _) in enumerate(LANGUAGES, 1): print(f"    [{index}] {name}")
    print("    [0] Exit")
    print()
    ui.bullet("Depending on the selected language, if a translation is available, it can be installed.")
    print()
    while True:
        choice = input("  > ").strip()
        if choice == "0": return ""
        if choice.isdigit() and 1 <= int(choice) <= len(LANGUAGES): return LANGUAGES[int(choice) - 1][1]
        ui.status_err("Invalid option")



def get_mods_list() -> dict:
    try:
        path = resource("mods_list.json")
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        pass
    # Fallback to hardcoded list
    return {
        "PTBR": {
            "Mediafire": "https://download1472.mediafire.com/h9gwt1ys5weglxBXy6QIdOY8YR4x08_bI8wD6wcg2kPsQvho92G__KoPwKwaLDexs2o2hj3qfegZr8JwOeiP0lTr1trKNWf85tkPmEMpTRUqsfqM-sT4rFMsgsFZ7Orw0-KKJANu7YYHPn1SKmdWshgF5R3iI9l-BBnK0OffcfKjoMg/4ildqbqja3ehuq2/PTBR.zip",
            "Mega": "https://mega.nz/file/ExcDiC6B#1pqrFjmz2-xaEiQ0SWOaa13N18k9sWevwHWIww21b8Q",
            "Google Drive": "https://drive.google.com/uc?export=download&id=1LCT2kD-WQpPQO3dAeIldXBUn2j3c0m9d",
            "Archive": "https://archive.org/download/ptbr_20260725/PTBR.zip"
        }
    }


def choose_extra_language(text: dict) -> str | None:
    print(f"\n  {text['extra_lang_prompt']}\n")
    print(f"    [1] {text['extra_lang_ptbr']}")
    print(f"    [2] {text['extra_lang_none']}")
    while True:
        choice = input("  > ").strip()
        if choice == "1": return "PTBR"
        if choice in ("2", ""): return None
        print(f"  {text['invalid_option']}")


def download_mod(url: str, dest: Path) -> None:
    ui.status_info(TX(getattr(ui, "active_lang", "EN"), "downloading_translation").format(url=url))
    req = urllib.request.Request(
        url,
        headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
    )
    with urllib.request.urlopen(req) as response, open(dest, 'wb') as out_file:
        length = response.getheader('content-length')
        if length:
            length = int(length)
            block_size = 64 * 1024
            downloaded = 0
            while True:
                buffer = response.read(block_size)
                if not buffer:
                    break
                out_file.write(buffer)
                downloaded += len(buffer)
                ui.progress_bar(downloaded, length, "Downloading")
        else:
            with ui.Spinner("Downloading") as spin:
                out_file.write(response.read())
                spin.stop()


def extract_mod(zip_path: Path, dest_dir: Path) -> None:
    """Extract a translation into the canonical Vita language tree.

    Existing PTBR.zip mirrors may either contain a PTBR/ root or put chapter
    files directly at the archive root. Both layouts normalize to
    mods/Lang/Portuguese-BR without preserving the legacy PTBR directory.
    """
    ui.status_info(TX(getattr(ui, "active_lang", "EN"), "extracting_translation"))
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        for member in zip_ref.infolist():
            if member.is_dir():
                continue
            parts = list(Path(member.filename.replace("\\", "/")).parts)
            if parts and parts[0].lower() == "ptbr":
                parts = parts[1:]
            if not parts:
                continue
            if parts[-1].lower() == "game.droid":
                parts[-1] = "data.win"
            target_path = dest_dir.joinpath(*parts)
            target_path.parent.mkdir(parents=True, exist_ok=True)
            with zip_ref.open(member) as source, open(target_path, "wb") as target:
                shutil.copyfileobj(source, target)


def find_umt_cli() -> Path | None:
    """Locate the optional UndertaleModCli tool used to export a mod's own
    texture pages. Not bundled inside the .exe (it is a large .NET toolchain);
    looked up next to the patcher so it can be dropped in separately."""
    candidate = app_dir() / "UndertaleModToolCLI" / "UndertaleModCli.exe"
    return candidate if candidate.is_file() else None


def optimize_mod_textures(text: dict, result: Path, mod_name: str) -> None:
    """Generate PVR-packed textures for the installed mod, chapter by chapter,
    reusing the base game's own .pvr files wherever the mod didn't touch a
    page. Degrades safely (prints a warning, changes nothing) if the texture
    optimizer isn't available - the mod still works via the on-Vita fallback."""
    draw_header(T(text, "scr_pvr"))
    umt_cli = find_umt_cli()
    csx_script = resource("patcher_scripts/ExportEmbeddedTexturesForPvr.csx")
    if umt_cli is None:
        ui.status_warn(T(text, "pvr_no_tool"))
        return

    mod_root = result / "deltarunevita" / "mods" / "Lang" / mod_name
    base_pvr_root = result / "deltarunevita" / "pvr"
    chapters = sorted(
        int(p.name.removeprefix("chapter")) for p in mod_root.glob("chapter*")
        if p.is_dir() and p.name.removeprefix("chapter").isdigit() and (p / "data.win").is_file()
    )
    if not chapters:
        ui.status_warn(T(text, "pvr_skip_no_pages"))
        return

    total_reused = 0
    total_generated = 0
    with tempfile.TemporaryDirectory() as tmp:
        work_dir = Path(tmp)
        for chapter in chapters:
            ui.section(T(text, "pvr_chapter").format(ch=chapter))
            mod_data_win = mod_root / f"chapter{chapter}" / "data.win"
            base_pvr_dir = base_pvr_root / f"chapter{chapter}"
            out_pvr_dir = mod_root / "pvr" / f"chapter{chapter}"
            if not base_pvr_dir.is_dir():
                ui.status_warn(T(text, "pvr_skip_no_pages"))
                continue

            def progress(done: int, total: int, label: str, _chapter=chapter) -> None:
                ui.progress_bar(done, total, f"ch{_chapter} {label}")

            try:
                results = pvr_pipeline.generate_mod_pvr(
                    mod_data_win=mod_data_win, base_pvr_dir=base_pvr_dir, out_pvr_dir=out_pvr_dir,
                    umt_cli=umt_cli, csx_script=csx_script, work_dir=work_dir / f"ch{chapter}",
                    progress_cb=progress,
                )
            except Exception as exc:
                ui.status_warn(f"{exc}")
                continue
            reused = sum(1 for r in results if r.action == "reused_base")
            generated = sum(1 for r in results if r.action == "generated")
            total_reused += reused
            total_generated += generated
            ui.status_ok(T(text, "pvr_summary").format(reused=reused, generated=generated))

    print()
    ui.status_ok(T(text, "pvr_done") + f" ({total_reused} reused / {total_generated} generated)")


def download_translation_mod(text: dict, result: Path) -> None:
    """Try every mirror in order and stop at the first one that yields a valid zip."""
    draw_header(T(text, "scr_mod"))
    mods_list = get_mods_list()
    mirrors = list(mods_list.get("PTBR", {}).items())
    temp_zip = app_dir() / "PTBR.zip"
    for name, url in mirrors:
        try:
            ui.status_info(f"{text['mod_trying']}: {name}")
            download_mod(url, temp_zip)
            if not zipfile.is_zipfile(temp_zip):
                raise ValueError("File is not a zip file")
            extract_mod(temp_zip, result / "deltarunevita" / "mods" / "Lang" / "Portuguese-BR")
            optimize_mod_textures(text, result, "Portuguese-BR")
            return
        except Exception as e:
            ui.status_err(f"{name}: {e}")
            continue
        finally:
            if temp_zip.is_file():
                temp_zip.unlink()
    print()
    ui.status_warn(text["mod_download_warn"])
    ui.bullet(text["mod_download_manual_hint"])



def load_language_catalog() -> dict:
    """Load the language catalog for this patcher version.

    Prefer the remote catalog only when it targets the same port version. This
    prevents a newly released patcher from silently offering stale language
    packs while GitHub Pages is still serving the previous release catalog.
    """
    try:
        req = urllib.request.Request(LANG_MANIFEST_URL, headers={"User-Agent": "DeltaruneVitaPatcher/" + VERSION})
        with urllib.request.urlopen(req, timeout=15) as response:
            remote = json.loads(response.read().decode("utf-8"))
        remote_version = str(remote.get("port_version", "")).removeprefix("v")
        expected_version = str(VERSION).removeprefix("v")
        if remote_version == expected_version:
            return remote
    except Exception:
        pass

    local = resource("language_packs.json")
    if local.is_file():
        return json.loads(local.read_text(encoding="utf-8"))
    return {"languages": []}


def choose_extra_languages(text: dict, interface_lang: str) -> list[dict]:
    catalog = load_language_catalog()
    available = [x for x in catalog.get("languages", []) if x.get("ready") and (x.get("url") or (isinstance(x.get("parts"), list) and x.get("parts")))]
    if not available:
        return []
    interface_code = {"PT":"ptbr","ES":"es","FR":"fr","RU":"ru","IT":"it","PTPT":"pt","EN":"en","JP":"ja"}.get(interface_lang, "en")
    selected: set[str] = set()
    suggested = next((x for x in available if interface_code in x.get("interface_codes", [])), None)
    if suggested:
        name = suggested.get("display_name", suggested["id"])
        print("\n  " + TX(interface_lang, "language_suggestion").format(name=name))
        answer = input("  [S/N] > ").strip().lower()
        if answer in {"s", "sim", "y", "yes"}:
            selected.add(suggested["id"])
    print("\n  " + TX(interface_lang, "optional_languages") + "\n")
    for i, entry in enumerate(available, 1):
        mark = "x" if entry["id"] in selected else " "
        print(f'    [{i}] [{mark}] {entry.get("display_name", entry["id"])}')
    original_note = TX(interface_lang, "original_language_note")
    print(f"\n  {original_note}")
    print("\n  " + TX(interface_lang, "toggle_languages"))
    raw = input("  > ").strip()
    if raw:
        for part in raw.replace(";", ",").split(","):
            part = part.strip()
            if part.isdigit() and 1 <= int(part) <= len(available):
                ident = available[int(part)-1]["id"]
                if ident in selected: selected.remove(ident)
                else: selected.add(ident)
    return [x for x in available if x["id"] in selected]


def apply_language_pack(entry: dict, steam: Path, result: Path, text: dict) -> None:
    lang_id = entry["id"]
    draw_header(TX(getattr(ui, "active_lang", "EN"), "language_pack").format(name=entry.get("display_name", lang_id)))
    with tempfile.TemporaryDirectory() as td:
        pack_path = Path(td) / (entry.get("file") or f"{lang_id}.langpack")
        parts = entry.get("parts") if isinstance(entry.get("parts"), list) else []
        if parts:
            with open(pack_path, "wb") as merged:
                for part_index, part in enumerate(parts, 1):
                    part_url = str(part.get("url", ""))
                    if not part_url:
                        raise ValueError(f"Language Pack part URL missing: {lang_id} part {part_index}")
                    if not urllib.parse.urlparse(part_url).scheme:
                        part_url = urllib.parse.urljoin(LANG_MANIFEST_URL, "../" + part_url.lstrip("/"))
                    part_path = Path(td) / f"part_{part_index:02d}.bin"
                    download_mod(part_url, part_path)
                    if part.get("size") and part_path.stat().st_size != int(part["size"]):
                        raise ValueError(f"Language Pack part size mismatch: {lang_id} part {part_index}")
                    if part.get("sha256") and sha256(part_path).lower() != str(part["sha256"]).lower():
                        raise ValueError(f"Language Pack part SHA-256 mismatch: {lang_id} part {part_index}")
                    with open(part_path, "rb") as source_part:
                        shutil.copyfileobj(source_part, merged)
        else:
            download_mod(entry["url"], pack_path)
        expected_pack_hash = str(entry.get("sha256", "")).lower()
        if expected_pack_hash and sha256(pack_path).lower() != expected_pack_hash:
            raise ValueError(f"Language Pack SHA-256 mismatch: {lang_id}")
        if not zipfile.is_zipfile(pack_path):
            raise ValueError(f"Invalid Language Pack: {lang_id}")
        with zipfile.ZipFile(pack_path, "r") as z:
            manifest = json.loads(z.read("manifest.json").decode("utf-8"))
            if manifest.get("language") != lang_id:
                raise ValueError(f"Language Pack manifest mismatch: {lang_id}")
            out_root = result / "deltarunevita" / "mods" / "Lang" / lang_id
            shutil.rmtree(out_root, ignore_errors=True)
            records = manifest.get("records", [])
            for index, rec in enumerate(records, 1):
                target = out_root / safe_path(rec["output"])
                target.parent.mkdir(parents=True, exist_ok=True)
                mode = rec["mode"]
                if mode in {"copy", "patch"}:
                    source = steam / safe_path(rec["source"])
                    if not source.is_file():
                        raise ValueError(f"Missing Steam source for {lang_id}: {rec['source']}")
                    if rec.get("source_size") and source.stat().st_size != rec["source_size"]:
                        raise ValueError(f"Incompatible Steam source for {lang_id}: {rec['source']}")
                    if rec.get("source_sha256") and sha256(source).lower() != rec["source_sha256"].lower():
                        raise ValueError(f"Incompatible Steam source for {lang_id}: {rec['source']}")
                    if mode == "copy":
                        shutil.copyfile(source, target)
                    else:
                        patch_member = rec["patch"]
                        patch_file = Path(td) / Path(patch_member).name
                        patch_file.write_bytes(z.read(patch_member))
                        bsdiff4.file_patch(str(source), str(target), str(patch_file))
                elif mode == "extra":
                    target.write_bytes(z.read(rec["extra"]))
                else:
                    raise ValueError(f"Unsupported Language Pack mode: {mode}")
                if target.stat().st_size != rec["size"] or sha256(target).lower() != rec["output_sha256"].lower():
                    raise ValueError(f"Language Pack output verification failed: {lang_id}/{rec['output']}")
                ui.progress_bar(index, max(1, len(records)), f"{lang_id}")
    ui.status_ok(TX(getattr(ui, "active_lang", "EN"), "language_installed").format(name=entry.get("display_name", lang_id)))

def main() -> int:
    os.system("chcp 65001 >nul")
    os.system("title DELTARUNE - PS Vita Patcher")
    ui.enable_ansi()
    lang = choose_language()
    if not lang: return 0
    text = TR[lang]

    draw_header(T(text, "scr_prepare"))
    ui.section(TX(lang, "official_game_data"))
    print(f"  {text['note']}\n")
    ui.bullet(TX(lang, "supported_steam"))
    ui.bullet("Output target: PS Vita / ux0:data/deltarune")
    ui.bullet("The patcher validates source files before generating Vita data.")
    print()
    ui.bullet(text['instruction'])
    ui.bullet(text['output'])
    print()
    try:
        manifest = json.loads(resource("patch_data/manifest.json").read_text(encoding="utf-8"))
        steam = find_steam(text)
        ui.status_ok(f"{text['found']}: {steam}")
        answer = input(f"\n  {text['confirm']} [{text['yes']}/N]: ").strip().upper()
        if answer != text["yes"]:
            ui.status_warn(text['cancelled'])
            input(f"\n  {text['close']}")
            return 0

        ui.section(T(text, "verifying_label"))
        verify_steam(steam, manifest, text)

        draw_header(T(text, "scr_generate"))
        result = generate(steam, manifest, text)

        draw_header(T(text, "scr_mod"))
        extra_languages = choose_extra_languages(text, lang)
        for language_entry in extra_languages:
            apply_language_pack(language_entry, steam, result, text)

        draw_header(T(text, "scr_done"))
        ui.status_ok(text['success'])
        print(f"\n  {result}\n")
        ui.bullet(text['copy'])
        code = 0
    except Exception as exc:
        print()
        ui.status_err(f"{text['error']}: {exc}")
        code = 1
    input(f"\n  {text['close']}")
    return code


if __name__ == "__main__": raise SystemExit(main())



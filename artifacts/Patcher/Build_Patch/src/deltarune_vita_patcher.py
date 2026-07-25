# -*- coding: utf-8 -*-
"""DELTARUNE PS Vita data patcher by WolffsRoom."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

import bsdiff4

VERSION = "0.63"
LANGUAGES = [
    ("English", "EN"), ("Portugues (Brasil)", "PT"), ("Espanol", "ES"),
    ("Francais", "FR"), ("Portugues (Portugal)", "PTPT"), ("Italiano", "IT"),
    ("Russian", "RU"), ("Japanese", "JP"),
]

TR = {
    "EN": dict(note="This changes only the language of this program. It does not change the game's language.",
        instruction="Copy an original, unmodified DELTARUNE Steam installation into SteamFiles.",
        output="The output will be created in VitaFiles\\deltarune.", found="Steam installation found",
        missing="No valid DELTARUNE Steam installation was found in SteamFiles.", confirm="Start generating VitaFiles?",
        yes="Y", cancelled="Operation cancelled.", verify="Verifying Steam files", incompatible="Incompatible or modified Steam file",
        generating="Generating Vita data", checkfail="Output verification failed", success="SUCCESS! All files were generated and verified.",
        copy="Copy the 'deltarune' folder to ux0:data/ on the PS Vita.", error="ERROR", close="Press ENTER to close..."),
    "PT": dict(note="Isso altera somente o idioma deste programa. Nao altera o idioma do jogo.",
        instruction="Copie uma instalacao original e sem modificacoes do DELTARUNE da Steam para SteamFiles.",
        output="A saida sera criada em VitaFiles\\deltarune.", found="Instalacao Steam encontrada",
        missing="Nenhuma instalacao valida do DELTARUNE foi encontrada em SteamFiles.", confirm="Iniciar a geracao do VitaFiles?",
        yes="S", cancelled="Operacao cancelada.", verify="Verificando arquivos da Steam", incompatible="Arquivo Steam incompativel ou modificado",
        generating="Gerando dados do Vita", checkfail="Falha ao verificar a saida", success="SUCESSO! Todos os arquivos foram gerados e verificados.",
        copy="Copie a pasta 'deltarune' para ux0:data/ no PS Vita.", error="ERRO", close="Pressione ENTER para fechar..."),
    "ES": dict(note="Esto solo cambia el idioma de este programa. No cambia el idioma del juego.",
        instruction="Copia una instalacion original de DELTARUNE de Steam dentro de SteamFiles.", output="La salida se creara en VitaFiles\\deltarune.",
        found="Instalacion de Steam encontrada", missing="No se encontro una instalacion valida de DELTARUNE en SteamFiles.",
        confirm="Iniciar la generacion de VitaFiles?", yes="S", cancelled="Operacion cancelada.", verify="Verificando archivos de Steam",
        incompatible="Archivo de Steam incompatible o modificado", generating="Generando datos de Vita", checkfail="Error al verificar la salida",
        success="EXITO! Todos los archivos fueron generados y verificados.", copy="Copia la carpeta 'deltarune' a ux0:data/ en PS Vita.", error="ERROR", close="Pulsa ENTER para cerrar..."),
    "FR": dict(note="Ceci change uniquement la langue de ce programme, pas celle du jeu.",
        instruction="Copiez une installation Steam originale de DELTARUNE dans SteamFiles.", output="La sortie sera creee dans VitaFiles\\deltarune.",
        found="Installation Steam trouvee", missing="Aucune installation DELTARUNE valide trouvee dans SteamFiles.", confirm="Commencer la creation de VitaFiles ?",
        yes="O", cancelled="Operation annulee.", verify="Verification des fichiers Steam", incompatible="Fichier Steam incompatible ou modifie",
        generating="Creation des donnees Vita", checkfail="Echec de verification de la sortie", success="SUCCES ! Tous les fichiers ont ete crees et verifies.",
        copy="Copiez le dossier 'deltarune' vers ux0:data/ sur PS Vita.", error="ERREUR", close="Appuyez sur ENTREE pour fermer..."),
    "PTPT": dict(note="Isto altera apenas o idioma deste programa. Nao altera o idioma do jogo.",
        instruction="Copie uma instalacao Steam original do DELTARUNE para SteamFiles.", output="A saida sera criada em VitaFiles\\deltarune.",
        found="Instalacao Steam encontrada", missing="Nao foi encontrada uma instalacao DELTARUNE valida em SteamFiles.", confirm="Iniciar a criacao de VitaFiles?",
        yes="S", cancelled="Operacao cancelada.", verify="A verificar ficheiros Steam", incompatible="Ficheiro Steam incompativel ou modificado",
        generating="A criar dados Vita", checkfail="Falha ao verificar a saida", success="SUCESSO! Todos os ficheiros foram criados e verificados.",
        copy="Copie a pasta 'deltarune' para ux0:data/ na PS Vita.", error="ERRO", close="Prima ENTER para fechar..."),
    "IT": dict(note="Questo cambia solo la lingua del programma, non quella del gioco.",
        instruction="Copia un'installazione Steam originale di DELTARUNE in SteamFiles.", output="I file verranno creati in VitaFiles\\deltarune.",
        found="Installazione Steam trovata", missing="Nessuna installazione DELTARUNE valida trovata in SteamFiles.", confirm="Avviare la generazione di VitaFiles?",
        yes="S", cancelled="Operazione annullata.", verify="Verifica dei file Steam", incompatible="File Steam incompatibile o modificato",
        generating="Generazione dati Vita", checkfail="Verifica dei file generati fallita", success="SUCCESSO! Tutti i file sono stati generati e verificati.",
        copy="Copia la cartella 'deltarune' in ux0:data/ sulla PS Vita.", error="ERRORE", close="Premi INVIO per chiudere..."),
    "RU": dict(note="Eto menyaet tolko yazyk programmy, no ne yazyk igry.", instruction="Skopiruyte originalnuyu Steam-versiyu DELTARUNE v SteamFiles.",
        output="Rezultat budet sozdan v VitaFiles\\deltarune.", found="Steam-versiya naydena", missing="V SteamFiles net podhodyashchey versii DELTARUNE.",
        confirm="Nachat sozdanie VitaFiles?", yes="Y", cancelled="Operatsiya otmenena.", verify="Proverka faylov Steam", incompatible="Fayl Steam izmenyon ili nesovmestim",
        generating="Sozdanie dannyh Vita", checkfail="Oshibka proverki rezultata", success="USPEH! Vse fayly sozdany i provereny.",
        copy="Skopiruyte papku 'deltarune' v ux0:data/ na PS Vita.", error="OSHIBKA", close="Nazhmite ENTER dlya vyhoda..."),
    "JP": dict(note="Kore wa kono puroguramu no gengo dake wo henkou shimasu. Geemu no gengo wa kawarimasen.",
        instruction="Steam no seiki DELTARUNE foruda wo SteamFiles ni kopii shite kudasai.", output="VitaFiles\\deltarune ni sakusei shimasu.",
        found="Steam foruda ga mitsukarimashita", missing="SteamFiles ni DELTARUNE ga arimasen.", confirm="VitaFiles wo sakusei shimasu ka?",
        yes="Y", cancelled="Kyanseru shimashita.", verify="Steam fairu kakunin-chu", incompatible="Steam fairu ga chigaimasu",
        generating="Vita deeta sakusei-chu", checkfail="Shutsuryoku kakunin ni shippai", success="SEIKOU! Subete no fairu wo sakusei kakunin shimashita.",
        copy="'deltarune' foruda wo PS Vita no ux0:data/ ni kopii shite kudasai.", error="ERAA", close="ENTER de shuuryou..."),
}


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
    for index, (relative, expected) in enumerate(sources.items(), 1):
        source = steam / safe_path(relative)
        if not source.is_file() or source.stat().st_size != expected["size"] or sha256(source) != expected["sha256"]:
            raise ValueError(f"{text['incompatible']}: {relative}")
        print(f"\r  {text['verify']}... {index * 100 // len(sources):3d}%", end="", flush=True)
    print()


def generate(steam: Path, manifest: dict, text: dict) -> Path:
    destination = app_dir() / "VitaFiles" / manifest["output_folder"]
    temporary = destination.with_name(destination.name + ".tmp")
    shutil.rmtree(temporary, ignore_errors=True)
    temporary.mkdir(parents=True)
    files = manifest["files"]
    try:
        for index, record in enumerate(files, 1):
            output = temporary / safe_path(record["output"])
            output.parent.mkdir(parents=True, exist_ok=True)
            source_name = record.get("source")
            source = steam / safe_path(source_name) if source_name else None
            if record["mode"] == "copy":
                if source is None: raise ValueError(record["output"])
                shutil.copyfile(source, output)
            else:
                old = source if source else temporary / ".empty"
                if source is None: old.write_bytes(b"")
                bsdiff4.file_patch(str(old), str(output), str(resource("patch_data/patches") / record["patch"]))
                if source is None: old.unlink(missing_ok=True)
            if output.stat().st_size != record["size"] or sha256(output) != record["sha256"]:
                raise ValueError(f"{text['checkfail']}: {record['output']}")
            print(f"\r  {text['generating']}... {index * 100 // len(files):3d}%", end="", flush=True)
        print()
        shutil.rmtree(destination, ignore_errors=True)
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
        "steam_version": "v0.0.250",
        "validated_ogg_files": len(ogg_files),
        "prepared_console_borders": len(borders),
        "console_border_size": "960x544",
    }
    report_path = destination / "deltarunevita" / "patcher-report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def draw_header() -> None:
    print("=" * 66)
    print("               DELTARUNE - PS VITA PATCHER")
    print(f"                         v{VERSION}")
    print("                    Patcher by WolffsRoom")
    print("=" * 66)


def choose_language() -> str:
    draw_header()
    print("\n  SELECT LANGUAGE / SELECIONE O IDIOMA\n")
    for index, (name, _) in enumerate(LANGUAGES, 1): print(f"    [{index}] {name}")
    print("    [0] Exit / Sair")
    print("\n  NOTE: This changes only this program's language, not the game.")
    print("  NOTA: Isso altera somente o idioma deste programa, nao do jogo.\n")
    while True:
        choice = input("  > ").strip()
        if choice == "0": return ""
        if choice.isdigit() and 1 <= int(choice) <= len(LANGUAGES): return LANGUAGES[int(choice) - 1][1]
        print("  Invalid option / Opcao invalida.")



def get_mods_list() -> dict:
    try:
        path = resource("mods_list.txt")
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        pass
    # Fallback to hardcoded list
    return {
        "PTBR": {
            "Mediafire": "https://download1472.mediafire.com/wq1sfbskzyhgPzEezrIa5jdDfX9Mut7e3yehfMKMTT5tXxRNA5X_1-Kh9O0LRfl6I_nVWA7RB9ZhyhfAEz2yf6qjBOpUN5bCodQ-juY-TV7E1WysP0VfsJh20Es2vJVICDQMP5VCRqMo65PP9SBEzMASqy0wHF_0Ma69YLJxga4KQUg/4ildqbqja3ehuq2/PTBR.zip",
            "Mega": "https://mega.nz/file/ExcDiC6B#1pqrFjmz2-xaEiQ0SWOaa13N18k9sWevwHWIww21b8Q",
            "Google Drive": "https://drive.google.com/uc?export=download&id=1LCT2kD-WQpPQO3dAeIldXBUn2j3c0m9d",
            "Archive": "https://archive.org/download/ptbr_20260725/PTBR.zip"
        }
    }


def choose_mirror(mods_list: dict) -> str:
    print("\n  SELECT DOWNLOAD MIRROR FOR PTBR MOD / SELECIONE O SERVIDOR DE DOWNLOAD DA TRADUÇÃO:\n")
    mirrors = list(mods_list["PTBR"].keys())
    for index, name in enumerate(mirrors, 1):
        print(f"    [{index}] {name}")
    print("\n  Default is [4] Archive.org / O padrão é [4] Archive.org")
    while True:
        choice = input("  > ").strip()
        if not choice: return mirrors[3] # default Archive
        if choice.isdigit() and 1 <= int(choice) <= len(mirrors):
            return mirrors[int(choice) - 1]
        print("  Invalid option / Opção inválida.")


def download_mod(url: str, dest: Path) -> None:
    print(f"  Downloading translation from {url}...")
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
                print(f"\r  Downloading... {downloaded * 100 // length:3d}%", end="", flush=True)
            print()
        else:
            out_file.write(response.read())


def extract_mod(zip_path: Path, dest_dir: Path) -> None:
    print("  Extracting translation...")
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        has_ptbr_root = any(name.lower().startswith("ptbr/") or name.lower().startswith("ptbr\\") for name in zip_ref.namelist())
        
        for member in zip_ref.infolist():
            if member.is_dir(): continue
            
            # Determine destination path
            if has_ptbr_root:
                target_path = dest_dir / member.filename
            else:
                target_path = dest_dir / "PTBR" / member.filename
                
            target_path.parent.mkdir(parents=True, exist_ok=True)
            with zip_ref.open(member) as source, open(target_path, "wb") as target:
                shutil.copyfileobj(source, target)


def main() -> int:
    os.system("chcp 65001 >nul")
    os.system("title DELTARUNE - PS Vita Patcher")
    os.system("color 0A")
    lang = choose_language()
    if not lang: return 0
    text = TR[lang]
    os.system("cls")
    draw_header()
    print(f"\n  NOTE: {text['note']}\n")
    print(f"  {text['instruction']}")
    print(f"  {text['output']}\n")
    try:
        manifest = json.loads(resource("patch_data/manifest.json").read_text(encoding="utf-8"))
        steam = find_steam(text)
        print(f"  {text['found']}: {steam}")
        answer = input(f"  {text['confirm']} [{text['yes']}/N]: ").strip().upper()
        if answer != text["yes"]: print(f"\n  {text['cancelled']}"); return 0
        verify_steam(steam, manifest, text)
        result = generate(steam, manifest, text)
        
        # If the user is running in Portuguese, download the translation files
        if lang in ("PT", "PTPT"):
            try:
                mods_list = get_mods_list()
                chosen_mirror = choose_mirror(mods_list)
                url = mods_list["PTBR"][chosen_mirror]
                temp_zip = app_dir() / "PTBR.zip"
                try:
                    download_mod(url, temp_zip)
                    extract_mod(temp_zip, result / "deltarunevita" / "mods")
                finally:
                    if temp_zip.is_file():
                        temp_zip.unlink()
            except Exception as e:
                print(f"\n  Warning: Could not automatically download translation mod: {e}")
                print("  You can still manually download it and place it in deltarunevita/mods.")

        print(f"\n  {text['success']}\n  {result}\n\n  {text['copy']}")
        code = 0
    except Exception as exc:
        print(f"\n  {text['error']}: {exc}")
        code = 1
    input(f"\n  {text['close']}")
    return code


if __name__ == "__main__": raise SystemExit(main())

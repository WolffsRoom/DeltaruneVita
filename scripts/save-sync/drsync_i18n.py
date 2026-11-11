"""Texts of the DELTARUNE Sync PC app in the languages the Vita port supports
(the same list as the prologue: English, Portuguese-BR, Japanese, Spanish,
French, German, Italian, Turkish, Russian)."""
import locale

LANGS = ["English", "Portuguese-BR", "Japanese", "Spanish", "French", "German",
         "Italian", "Turkish", "Russian"]

NAMES = {"English": "English", "Portuguese-BR": "Português", "Japanese": "日本語",
         "Spanish": "Español", "French": "Français", "German": "Deutsch",
         "Italian": "Italiano", "Turkish": "Türkçe", "Russian": "Русский"}

T = {
 "searching": {"English": "Looking for the PS Vita...", "Portuguese-BR": "Procurando o PS Vita na rede...",
     "Japanese": "ネットワークでPS Vitaを探しています...", "Spanish": "Buscando la PS Vita en la red...",
     "French": "Recherche de la PS Vita...", "German": "Suche die PS Vita im Netzwerk...",
     "Italian": "Cerco la PS Vita nella rete...", "Turkish": "Ağda PS Vita aranıyor...",
     "Russian": "Ищу PS Vita в сети..."},
 "searching_sub": {"English": "Turn on Sync saves in the game", "Portuguese-BR": "Ligue Sincronizar saves no jogo",
     "Japanese": "ゲームでセーブ同期をオンにしてください", "Spanish": "Activa la sincronización en el juego",
     "French": "Activez la synchro dans le jeu", "German": "Sync im Spiel einschalten",
     "Italian": "Attiva la sincronia nel gioco", "Turkish": "Oyunda kayıt eşitlemeyi aç",
     "Russian": "Включи синхронизацию в игре"},
 "connected": {"English": "Connected to the PS Vita", "Portuguese-BR": "Conectado ao PS Vita",
     "Japanese": "PS Vitaに接続しました", "Spanish": "Conectado a la PS Vita", "French": "Connecté à la PS Vita",
     "German": "Mit der PS Vita verbunden", "Italian": "Connesso alla PS Vita", "Turkish": "PS Vita'ya bağlandı",
     "Russian": "Подключено к PS Vita"},
 "received": {"English": "Save received from the PS Vita", "Portuguese-BR": "Save recebido do PS Vita",
     "Japanese": "PS Vitaからセーブを受信", "Spanish": "Partida recibida de la PS Vita",
     "French": "Sauvegarde reçue de la Vita", "German": "Spielstand von der Vita erhalten",
     "Italian": "Salvataggio ricevuto dalla Vita", "Turkish": "PS Vita'dan kayıt alındı",
     "Russian": "Сохранение получено с PS Vita"},
 "sent": {"English": "Save sent to the PS Vita", "Portuguese-BR": "Save enviado para o PS Vita",
     "Japanese": "PS Vitaにセーブを送信", "Spanish": "Partida enviada a la PS Vita",
     "French": "Sauvegarde envoyée à la Vita", "German": "Spielstand an die Vita gesendet",
     "Italian": "Salvataggio inviato alla Vita", "Turkish": "Kayıt PS Vita'ya gönderildi",
     "Russian": "Сохранение отправлено на PS Vita"},
 "equal": {"English": "Saves match on both sides", "Portuguese-BR": "Saves iguais nos dois lados",
     "Japanese": "セーブは両方で同じです", "Spanish": "Las partidas son iguales",
     "French": "Les sauvegardes sont identiques", "German": "Spielstände sind gleich",
     "Italian": "Salvataggi uguali su entrambi i lati", "Turkish": "Kayıtlar iki tarafta da aynı",
     "Russian": "Сохранения совпадают"},
 "waiting": {"English": "Waiting for DELTARUNE to close", "Portuguese-BR": "Aguardando o DELTARUNE fechar",
     "Japanese": "PCのDELTARUNEが閉じるのを待っています", "Spanish": "Esperando que DELTARUNE se cierre",
     "French": "En attente de la fermeture de DELTARUNE", "German": "Warte auf das Ende von DELTARUNE",
     "Italian": "Attendo la chiusura di DELTARUNE", "Turkish": "PC'de DELTARUNE'un kapanması bekleniyor",
     "Russian": "Жду закрытия DELTARUNE на ПК"},
 "sync": {"English": "Sync", "Portuguese-BR": "Sincronizar", "Japanese": "同期", "Spanish": "Sincronizar",
     "French": "Synchro", "German": "Sync", "Italian": "Sincr.", "Turkish": "Eşitle", "Russian": "Синхр."},
 "backups": {"English": "Backups", "Portuguese-BR": "Backups", "Japanese": "バックアップ", "Spanish": "Copias",
     "French": "Copies", "German": "Backups", "Italian": "Backup", "Turkish": "Yedekler", "Russian": "Копии"},
 "hide": {"English": "Hide", "Portuguese-BR": "Ocultar", "Japanese": "隠す", "Spanish": "Ocultar",
     "French": "Masquer", "German": "Ausblenden", "Italian": "Nascondi", "Turkish": "Gizle", "Russian": "Скрыть"},
 "open": {"English": "Open", "Portuguese-BR": "Abrir", "Japanese": "開く", "Spanish": "Abrir", "French": "Ouvrir",
     "German": "Öffnen", "Italian": "Apri", "Turkish": "Aç", "Russian": "Открыть"},
 "sync_now": {"English": "Sync now", "Portuguese-BR": "Sincronizar agora", "Japanese": "今すぐ同期",
     "Spanish": "Sincronizar ahora", "French": "Synchroniser", "German": "Jetzt synchronisieren",
     "Italian": "Sincronizza ora", "Turkish": "Şimdi eşitle", "Russian": "Синхронизировать"},
 # ---- DELTARUNE Debug (developer tool) ----
 "dbg_sub": {"English": "Turn on Send reports locally (DEBUG)", "Portuguese-BR": "Ligue Enviar reports local (DEBUG)",
     "Japanese": "DEBUGでローカル送信をオンにしてください", "Spanish": "Activa Enviar reportes local (DEBUG)",
     "French": "Activez Envoi local des rapports (DEBUG)", "German": "Berichte lokal senden (DEBUG) an",
     "Italian": "Attiva Invia report in locale (DEBUG)", "Turkish": "DEBUG: Raporlari yerel gonder'i ac",
     "Russian": "Включи локальную отправку (DEBUG)"},
 "dbg_watch": {"English": "Watching new logs and screenshots", "Portuguese-BR": "Aguardando novos logs e capturas",
     "Japanese": "新しいログとスクショを待っています", "Spanish": "Esperando registros y capturas nuevas",
     "French": "En attente de journaux et captures", "German": "Warte auf neue Logs und Screenshots",
     "Italian": "In attesa di nuovi log e screenshot", "Turkish": "Yeni log ve ekran goruntusu bekleniyor",
     "Russian": "Жду новые логи и скриншоты"},
 "dbg_log": {"English": "Devlog received", "Portuguese-BR": "Devlog recebido", "Japanese": "Devlogを受信",
     "Spanish": "Devlog recibido", "French": "Devlog reçu", "German": "Devlog erhalten", "Italian": "Devlog ricevuto",
     "Turkish": "Devlog alindi", "Russian": "Devlog получен"},
 "dbg_shot": {"English": "Screenshot received", "Portuguese-BR": "Captura recebida", "Japanese": "スクショを受信",
     "Spanish": "Captura recibida", "French": "Capture reçue", "German": "Screenshot erhalten",
     "Italian": "Screenshot ricevuto", "Turkish": "Ekran goruntusu alindi", "Russian": "Скриншот получен"},
 "live": {"English": "Live log", "Portuguese-BR": "Log ao vivo", "Japanese": "ライブログ", "Spanish": "Log en vivo",
     "French": "Log direct", "German": "Live-Log", "Italian": "Log live", "Turkish": "Canli log", "Russian": "Лог"},
 "folder": {"English": "Folder", "Portuguese-BR": "Pasta", "Japanese": "フォルダ", "Spanish": "Carpeta",
     "French": "Dossier", "German": "Ordner", "Italian": "Cartella", "Turkish": "Klasor", "Russian": "Папка"},
 "quit": {"English": "Quit", "Portuguese-BR": "Sair", "Japanese": "終了", "Spanish": "Salir", "French": "Quitter",
     "German": "Beenden", "Italian": "Esci", "Turkish": "Çıkış", "Russian": "Выход"},
}

def system_language():
    """Best match for the Windows display language, English otherwise."""
    try:
        import ctypes
        code = locale.windows_locale.get(ctypes.windll.kernel32.GetUserDefaultUILanguage(), "")
    except Exception:
        code = (locale.getlocale()[0] or "")
    code = code.lower()
    table = {"pt": "Portuguese-BR", "ja": "Japanese", "es": "Spanish", "fr": "French", "de": "German",
             "it": "Italian", "tr": "Turkish", "ru": "Russian"}
    return table.get(code[:2], "English")

def tr(key, lang):
    row = T.get(key, {})
    return row.get(lang) or row.get("English") or key

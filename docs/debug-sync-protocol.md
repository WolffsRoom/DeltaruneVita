# DELTARUNE Debug: logs do Vita no PC (v0.84-3)

Ferramenta de desenvolvimento e testes. Junta, sem FTP, o que antes exigia abrir o
VitaShell e copiar arquivos: devlogs, capturas de tela e o log do probe em tempo real.

## Uso

1. **Vita:** Configurações do port > DEBUG > **Enviar reports local** = Ligado
   (`debug_report_sync=1` no `ux0:data/deltarune/config.ini`). Vem desligado. Funciona no
   VitaGL e no Native GXM (o GXM lê a mesma chave do `config.ini`).
2. **PC:** `DeltaruneDebug.exe` (ou `python scripts\save-sync\deltarune_vita_debug.py`), na
   mesma rede (Wi-Fi ou Ethernet). Fica na bandeja com o coração amarelo, que pulsa
   quando o Vita está conectado.
3. Menu: **Log ao vivo** (janela com o log atual), **Pasta** (arquivos recebidos),
   idioma e Ocultar.

Os arquivos vão para `Documentos\DeltaruneVita Debug\`:

| Pasta | Origem no Vita | O que entra |
|---|---|---|
| `devlogs\` | `ux0:data/deltarune/deltarunevita/devlogs/` (com subpastas) | só arquivos novos |
| `screenshots\` | `ux0:picture/SCREENSHOT/Deltarune/` | só capturas novas |
| `live\` | `native-gxm-probe.log` ou `butterscotch-probe.log` | cópia do que o visor mostrou |

"Novo" = criado ou alterado depois da primeira conexão desta execução do app. O que já
existia quando o app conectou não é copiado. Um arquivo só é baixado quando o tamanho
fica igual em duas listagens seguidas (o jogo pode estar escrevendo). O visor segue o log
com a data de modificação mais recente: `native-gxm-probe.log` quando o GXM está ativo,
`butterscotch-probe.log` no VitaGL.

O Vita nunca recebe arquivos por esse canal. Estado e log do app:
`%LOCALAPPDATA%\DeltaruneVitaDebug\` (`debug.log`, `settings.json`).

## Protocolo

Portas próprias, separadas do Save Sync (26540/26541), para os dois rodarem juntos.

- **Descoberta:** o Vita manda `DRDEBUG 1 26551 PSVita\n` em broadcast UDP na porta
  **26550** a cada ~2 s.
- **TCP 26551**, comandos em linhas de texto:
  - `HELLO DRDEBUG 1` → `OK DRDEBUG 1 PSVita`
  - `LIST` → `FILE <devlog|shot> <tamanho> <mtime> <caminho relativo>` por arquivo, depois `END`
  - `GET <devlog|shot> <caminho relativo>` → `DATA <n>` + n bytes, ou `ERR ...`
  - `LOGS` → `LOG <nome> <tamanho> <mtime>` para os dois logs que existirem, depois `END`
  - `TAIL <nome> <offset>` → `DATA <n> <total>[ RESET]` + até 64 KiB a partir do offset.
    `RESET`: o log ficou menor que o offset (rotacionado/recriado) e a leitura voltou ao 0.
  - `BYE`
- Caminhos com `..`, `:` ou começando com `/` são recusados; `TAIL` só aceita os dois logs.
- `mtime` no Vita é `AAAAMMDDhhmmss`; só é comparado com ele mesmo.

## Código

- Vita: `src/vita-probe/source/vita_debug_sync.c/.h` (thread de prioridade baixa; também
  compilado em `experiments/native-gxm`). Opção em `vita_settings.c` (item 4 da aba DEBUG,
  que antes era "Enviar log atual" por HTTP, nunca configurado).
- PC: `scripts/save-sync/deltarune_vita_debug.py`, reaproveitando a janela de
  `drsync_ui.py` (`configure()` troca nome, título e cor do coração).
- Teste local: `scratch/review-v081/debugtest.cmd` compila o mesmo C como servidor Windows
  (`-DDEBUG_SYNC_DESKTOP_TEST`) e `debugtest.py` faz o teste de ponta a ponta.

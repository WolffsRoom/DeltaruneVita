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

## Auto-teste (v0.86)

Com o jogo aberto no Vita, em gameplay no capítulo do roteiro, e **Enviar reports local** ligado, o PC
consegue navegar pelas salas sozinho e juntar os logs, sem nenhum toque no Vita:

```
python scripts\save-sync\deltarune_vita_debug.py --autotest ch5
python scripts\save-sync\deltarune_vita_debug.py --autotest meu-roteiro.txt --seconds 30
python scripts\save-sync\deltarune_vita_debug.py --status
python scripts\save-sync\deltarune_vita_debug.py --rooms
```

(o mesmo vale para `DeltaruneDebug.exe`). Para cada sala do roteiro, o auto-teste:

1. marca o log (`DEBUG_REMOTE=mark text=autotest_<sala>`);
2. pede a sala pelo mesmo caminho do navegador de salas DEV (L+Triângulo). Salas que dependem da história
   são redirecionadas por `vitaDevSafeNavigatorTarget`, e o relatório mostra `pedida → real`. O pedido é
   recusado se houver menu aberto ou outra troca de sala em curso;
3. espera a sala assentar (4 s) e grava um devlog de N segundos (DEV_SAMPLE + profiler GML);
4. segue o log do probe ao vivo durante tudo.

No fim, baixa os devlogs gerados e grava em `Documentos\DeltaruneVita Debug\autotest\<data-hora>\`:
`report.md` (FPS médio/mínimo, quadro médio/máximo e os 2 scripts GML mais caros por sala),
`devlogs\` e `probe-during-test.log`.

Roteiros recomendados (`scripts/save-sync/autotest/`), um por capítulo, com as salas mais pesadas ou
representativas: `ch1.txt` … `ch5.txt`. Formato: uma sala por linha, segundos opcionais, `#` para
comentários. Salas que não existem no capítulo aberto são puladas.

Limitações: não dispara batalhas nem cutscenes (só visita salas). Funciona no VitaGL; no Native GXM o
servidor responde, mas os comandos não são aplicados.

### Comandos novos do protocolo

- `STATUS` → `STATUS room=<nome> index=<i> chapter=<c> frame=<f> fps=<x> pending=<0|1> capture=<0|1> menu=<0|1> last=<resultado> build=<versão>`
  (publicado pelo jogo a cada 15 quadros). `last` é `room_jump:<sala>`, `room_already:`,
  `room_unavailable:`, `room_busy:` ou `capture:<s>`.
- `ROOMS` → `ROOM <índice> <nome>` por sala do capítulo, depois `END`.
- `CMD ROOM <nome>`, `CMD CAPTURE <segundos>`, `CMD CAPTURE_STOP`, `CMD MARK <texto>` → `OK <seq>`
  (ou `ERR busy` com a fila de 8 cheia).

Cada cliente do PC tem a sua própria thread no Vita (até 3), então o app na bandeja e o auto-teste
funcionam conectados ao mesmo tempo. Teste local: `scratch/review-v081/autotest_e2e.py`, contra o
servidor de teste, que simula o jogo.
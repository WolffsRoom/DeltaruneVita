# API interna / superfícies de integração

Este projeto não expõe uma API HTTP. "API" aqui significa as **superfícies de integração
estáveis** do projeto: os pontos que scripts, build e runtime usam para se comunicar entre si e
que não devem mudar sem atualizar todos os consumidores. Trate como o "contrato" entre as partes
do pipeline descrito em [arquitetura.md](arquitetura.md).

## CLI dos scripts (PowerShell)

| Script | Uso | Entrada | Saída |
|---|---|---|---|
| `scripts/prepare-windows-data.ps1` | Preparar dados a partir da instalação Steam legítima | `SteamFiles/DELTARUNE/` | `data/prepared/deltarune/deltarunevita/` |
| `scripts/prepare-vita-mods.ps1` | Empacotar mods (ex.: tradução PT-BR) | `mods/PTBR/` | mesclado em `data/prepared/...` |
| `scripts/build-butterscotch-probe.ps1` | Build ativo do VPK (Docker + VitaSDK) | fontes em `src/`, dados preparados | `artifacts/current/Deltarune-v<versão>.vpk` |
| `scripts/build-deltarune.ps1` | **Legado** — build do SoLoader antigo | `src/legacy/soloader` | não é o pipeline atual |
| `scripts/prepare-deltarune-data.ps1` | **Legado** — validação de assets extraídos de APK | — | ligado à abordagem Android abandonada |

Todos rodam via `powershell -ExecutionPolicy Bypass -File .\scripts\<nome>.ps1`.

## Opções de build (CMake, `src/vita-probe/CMakeLists.txt`)

- `PLAYABLE_RUNNER` (padrão `ON`) — build completo do runner Butterscotch para Vita. É o alvo usado
  em produção.
- `VITAGL_PROBE` (padrão `OFF`) — build isolado de diagnóstico do VitaGL, sem o runner do jogo.
- Se nenhuma das duas estiver ativa, cai no alvo mínimo (`source/main.c`), um parser de `data.win`
  com saída em `debugScreen.c`, usado só para depuração de baixo nível.

Metadados fixos do pacote:
- `TITLE ID`: `DLTVITA01`
- `param.sfo`: `ATTRIBUTE2=12`
- `VERSION` do VPK (CMake) deve ser mantida em sincronia com `PORT_BUILD_VERSION` em
  `src/vita-probe/source/playable_main.c`.

## Layout de dados no dispositivo (contrato com o runtime)

```text
ux0:data/deltarune/
├── config.ini                          # settings persistidos (vídeo/áudio/controles)
├── save/                                # saves do jogo
└── deltarunevita/
    ├── chapter0/ … chapter5/            # data.win (sem rename) + assets por capítulo
    ├── music/
    ├── borders/
    └── mods/                            # mods preparados (ex.: PTBR), um por capítulo
```

- `config.ini` é lido/escrito por `src/vita-probe/source/vita_settings.c`
  (`SETTINGS_PATH = "ux0:data/deltarune/config.ini"`).
- Log de execução: `ux0:data/deltarune/deltarunevita/butterscotch-probe.log` — é o principal
  artefato para diagnosticar crashes e builtins faltantes; sempre peça esse arquivo ao reportar bugs.
- Diretórios de save de versões antigas (`v0.53`–`v0.56`, mixed-case) são migrados automaticamente
  na inicialização (ver comentário em `playable_main.c:357`) — ao mexer nesse código, preserve a
  migração para não quebrar saves de usuários antigos.

## Superfície de builtins do GML (Butterscotch)

`src/butterscotch/src/vm_builtins.c` implementa as funções builtin do GameMaker Language que o
bytecode de Deltarune chama. Builtins não implementados aparecem no log em tempo de execução — a
prática do projeto é **só implementar o que aparece no log**, não tentar prever builtins que talvez
nunca sejam usados (evita trabalho especulativo e superfície de bugs desnecessária).

## Troca de capítulo (runtime → runtime)

O seletor de capítulos chama `game_change` (builtin do GameMaker). O `vita-probe` intercepta essa
solicitação, grava o capítulo pedido em disco e **reinicia o próprio eboot**, que em seguida lê o
`data.win` do capítulo salvo. Esse é o único mecanismo de troca de capítulo — não existe hot-swap
de VM em memória.

## Mods: mirrors de download e mapeamento de arquivos

`mods/mods_list.txt` é a fonte única de verdade para os links de download de mods de idioma (hoje só
`PTBR`). É um JSON `{"<mod>": {"<nome do mirror>": "<url>"}}`; a ordem das chaves define a ordem
exibida nos patchers. Consumido por três lugares, que devem ficar sincronizados com esse arquivo:

- **Patcher desktop** (`deltarune_vita_patcher.py`, `get_mods_list()`): embutido no `.exe` via
  PyInstaller (`DeltaruneVitaPatcher.spec`, entrada `datas`) e lido em runtime via `resource(...)`;
  tem um fallback hardcoded idêntico caso o arquivo não seja encontrado.
- **Web Patcher** (`index.html` na branch `gh-pages`): faz `fetch("mods_list.json")` (cópia gerada a
  partir do mesmo conteúdo) com o mesmo fallback hardcoded embutido no script.
- Ambos baixam o `.zip` do mod e extraem **sem transformar nomes de arquivo** — o `.zip` já deve
  conter `chapterN/data.win` (e, opcionalmente, `chapter0/data.win` para o seletor) prontos para cair
  em `deltarunevita/mods/<nome>/`. Não há passo de rename `data.win` → `game.droid` em nenhum dos
  dois patchers (ver [arquitetura.md](arquitetura.md#convenção-de-nome-de-arquivo-datawin-sem-rename-para-gamedroid)).

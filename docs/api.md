# API interna / superfícies de integração

Este projeto não expõe uma API HTTP. "API" aqui significa as **superfícies de integração
estáveis** do projeto: os pontos que scripts, build e runtime usam para se comunicar entre si e
que não devem mudar sem atualizar todos os consumidores. Trate como o "contrato" entre as partes
do pipeline descrito em [arquitetura.md](arquitetura.md).

## Patchers (não são scripts PowerShell — ver detalhes na tabela de scripts abaixo)

Três formas de gerar `VitaFiles/deltarune`, todas fonte em `artifacts/Patcher/Build_Patch/`
(desktop + Seam's) ou na branch `gh-pages` (Web), compiladas via `Build_Patcher.bat`:

| Executável | Como funciona | Depende de internet para abrir? |
|---|---|---|
| `DeltaruneVitaPatcher_64bit.exe` / `_32bit.exe` | Console Python, lê `SteamFiles/` local, gera `VitaFiles/` local | Não (só para baixar mod de tradução opcional) |
| `SeamsPatcher_64bit.exe` / `_32bit.exe` | Sobe um servidor HTTP local (`http://127.0.0.1:<porta>`) servindo uma cópia estática do Web Patcher e abre no navegador padrão | Não (só para baixar mod de tradução opcional) |
| Web Patcher (GitHub Pages, branch `gh-pages`) | Mesma UI do Seam's Patcher, hospedada publicamente | Sim (depende do GitHub Pages estar no ar) |

Todos os três geram a mesma estrutura de saída e usam o mesmo `manifest.json`/`manifest.js`.

## CLI dos scripts (PowerShell)

| Script | Uso | Entrada | Saída |
|---|---|---|---|
| `scripts/prepare-windows-data.ps1` | Preparar dados a partir da instalação Steam legítima | `SteamFiles/DELTARUNE/` | `data/prepared/deltarune/deltarunevita/` |
| `scripts/prepare-vita-mods.ps1` | **Opcional/dev** — empacotar mod local sem depender do download do patcher | `mods/PTBR/` | mesclado em `data/prepared/...` |
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
- `TITLE ID`: `DELT00001`
- `param.sfo`: `ATTRIBUTE2=12`
- A versão-base é definida somente em `config/version.txt`. O script de build calcula o sufixo interno e injeta `PORT_BUILD_VERSION`; o CMake deriva a versão do pacote a partir da mesma fonte. Referência de runtime:
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
- Ambos baixam o `.zip` do mod e extraem para `deltarunevita/mods/<nome>/`. O `.zip` hospedado hoje
  ainda contém `chapterN/game.droid` (convenção antiga); por compatibilidade, os dois patchers
  renomeiam qualquer `game.droid` encontrado dentro do `.zip` para `data.win` no momento da extração
  (`extract_mod()` no desktop, trecho de merge do mod em `finishGeneration()` no Web Patcher) — não é
  necessário reempacotar o `.zip` hospedado, mas é recomendado fazê-lo por consistência (ver
  [arquitetura.md](arquitetura.md#convenção-de-nome-de-arquivo-datawin-sem-rename-para-gamedroid)).

## Manifest do patcher (`manifest.json`/`manifest.js`)

Gerado por `build_patch_data.py` a partir de `data/prepared/deltarune/` (saída do
`prepare-windows-data.ps1`) comparado com `SteamFiles/DELTARUNE`. Cada entrada de `files[]` tem um
`mode` que diz como o patcher deve produzir aquele arquivo de saída:

| `mode` | Significa | Como o patcher desktop resolve | Como o Web Patcher resolve |
|---|---|---|---|
| `copy` | Byte-idêntico a um arquivo da instalação Steam do usuário (`source`) | copia direto do Steam local | lê via File System Access API e copia pro zip |
| `patch` | Precisa de um binário `bsdiff` contra um `source` da Steam (diff real) | aplica `bsdiff4.file_patch` usando `patch_data/patches/<record.patch>` | **não suportado** — não há `source`/`bspatch` na Web hoje; nenhum registro usa esse modo atualmente, mas se algum passar a usar, o Web Patcher loga erro em vez de gerar silenciosamente um arquivo faltante |
| `embed` | Arquivo exclusivo do Vita, sem equivalente na Steam (`source: null`) — ex.: `ui/`, `sounds/`, `borders/` | copia de `patch_data/embedded/<record.output>` (bundled no `.exe`) | faz `fetch("embedded/<record.output>")` (arquivos publicados junto do `manifest.js` na branch `gh-pages`) |

Antes da v0.64 esses arquivos `embed` eram gerados como `patch` (bsdiff contra um arquivo vazio, só
para reaproveitar o mesmo mecanismo do desktop) — isso funcionava no `.exe` mas o Web Patcher nunca
implementou aplicação de `bsdiff`/bzip2 em JS, então esses arquivos ficavam **faltando silenciosamente**
no zip gerado pela Web (`ui/`, `sounds/`, `borders/` ausentes). Como esses arquivos não têm um `source`
Steam pra diferenciar contra, o bsdiff não trazia nenhum ganho de tamanho — por isso passaram a ser
bundled como `embed` (cópia direta), eliminando a dependência de bsdiff no caminho Web sem precisar
implementar um decodificador bzip2/bspatch em JS.

# CLAUDE.md

Guia para agentes de IA (Claude Code e outros) trabalhando neste repositório. Leia isto antes de
mexer no código ou na documentação.

## Visão geral

DeltaruneVita é um port não-oficial de DELTARUNE (GameMaker) para PS Vita. Não usa o YoYo/GameMaker
runtime original: reimplementa o runner do GameMaker ("Butterscotch") do zero em C, com VitaGL para
renderização. O jogo original (`data.win`) é interpretado por uma VM própria (`src/butterscotch/src/vm.c`)
que lê bytecode/recursos do formato GameMaker.

**Regra de manutenção**: toda vez que uma mudança relevante de arquitetura, pipeline de build ou
comportamento for feita, atualize a documentação correspondente na mesma sessão. Não deixe para depois.

| Arquivo | Conteúdo |
|---|---|
| `docs/arquitetura.md` | Decisões técnicas, otimizações de renderização/memória, histórico de bugs difíceis |
| `docs/roadmap.md` | Planejamento de versões futuras |
| `docs/backlog.md` | Itens pendentes — mover para `changelog.md` quando concluídos |
| `docs/api.md` | Superfícies internas (funções, structs) do Butterscotch e do vita-probe |
| `docs/changelog.md` | Fonte de verdade de "o que mudou" por versão (o README tem um resumo espelhado) |
| `docs/PROGRESS.md` | Estado corrente do trabalho em andamento |

## Histórico resumido

1. **YoYo Loader** — tentativa inicial de rodar o runtime oficial do GameMaker no Vita. Abandonado.
2. **SoLoader** — segunda tentativa, carregando/patchando o runtime oficial. Abandonado.
3. **Butterscotch nativo** (atual) — reimplementação própria do runner GameMaker em C + VitaGL.
   Todo o progresso real do projeto está nesta abordagem.

Resíduos das tentativas abandonadas (YoYo/SoLoader) foram movidos para `src/legacy/` e
`tools/yoyoloader-builder/` — não são parte do pipeline ativo, mas foram preservados como referência
histórica. Não confunda esse código com o runner atual em `src/butterscotch/`.

## Estrutura do repositório

- `src/butterscotch/` — o runner GameMaker reimplementado (VM, decodificador de imagens, renderer
  OpenGL comum/legacy, áudio OpenAL, etc.). Compila tanto para desktop (debug) quanto para Vita.
- `src/vita-probe/` — o executável real do Vita (`playable_main.c`, `vita_settings.c`,
  `vita_borders.c`, `vita_video.c`). É quem decide troca de capítulo, configurações em tela,
  bordas/moldura da tela, etc.
- `src/common_args.c/.h` — parsing de argumentos de linha de comando compartilhado entre os
  executáveis Butterscotch (desktop/debug e Vita), extraído para evitar duplicação entre
  `runner.c` e os probes.
- `src/ButterscotchPreprocessor-main/` — ferramenta externa (Kotlin, projeto de terceiros) usada
  para pré-processar `data.win` (conversão de imagens, atlas, etc.) antes do empacotamento para o Vita.
- `third_party/vitaGL-nosplash/` — fork do VitaGL sem splashscreen, usado no build do Vita.
- `tools/` — scripts CLI do UndertaleModTool (`.csx`) para inspecionar/exportar recursos do
  `data.win` (texturas por página, shaders, egg logic, etc.) e o `UndertaleModTool`/`UndertaleModToolCLI`
  em si, usados para investigação manual de assets.
- `artifacts/Patcher/Build_Patch/src/` — os patchers de distribuição (`deltarune_vita_patcher.py`,
  `build_patch_data.py`, `DeltaruneVitaPatcher.spec`). Ficam **fora do branch `main`** e são
  gitignored — não são parte do código-fonte do jogo, mas da ferramenta de distribuição do patch
  para quem já possui o jogo original.

## Pipeline de build

- `scripts/prepare-windows-data.ps1` — prepara os arquivos de dados (extração/organização de
  `data.win` por capítulo) a partir de uma instalação Windows do jogo.
- `scripts/prepare-deltarune-data.ps1` / `scripts/prepare-vita-mods.ps1` — preparam dados finais e
  mods para o pacote do Vita.
- `scripts/build-deltarune.ps1` — build principal do jogo (não-probe).
- `scripts/build-butterscotch-probe.ps1` — builda o `vita-probe` via Docker
  (`atamanenko/vitasdk-softfp`), gera o VPK em `artifacts/current/Deltarune-v0.68.vpk` **e faz
  backup automático numerado do projeto** (ver seção "Backup automático" abaixo).

### Pipeline de textura PVR (novo, v0.65+)

Scripts novos relacionados à otimização de textura, complementando o pipeline acima:

- `scripts/preprocess_data.py` — invoca o `ButterscotchPreprocessor-main` (Kotlin/CLI) sobre um
  `data.win`, gerando atlas de textura com tamanho configurável (`atlas_size`, padrão 2048) em
  `data/preprocessed/`.
- `scripts/build-2048-pvr-cache.ps1` — usa o `PVRTexToolCLI` (Imagination/PowerVR Tools, ferramenta
  externa fixa em `C:\Program Files\Imgtec\PowerVR_Tools\...`) para converter os atlas de
  `data/pvr-work/` em `.pvr` comprimidos por hardware, gravando o resultado em `data/pvr/`, por capítulo.
- `scripts/validate-pvr-assets.ps1` — varre `data/pvr/` recursivamente e valida a integridade dos
  arquivos `.pvr` gerados.
- `scripts/generate_texture_test.py` — gera um recorte de teste de uma página de textura específica
  (`target_page`) a partir de um `data.win` de capítulo, para depuração visual isolada.
- `scripts/convert_ps4_borders.py` — converte as molduras/bordas de tela (asset "Deltarune Borders –
  Console Version") para o formato preparado em `data/prepared/.../borders`, usando o PVRTexToolCLI.
- `scripts/convert-select-icons.py` — reindexação/quantização (MEDIANCUT, paleta de 255 cores) dos
  ícones da tela de seleção, preservando canal alfa separado.
- Pastas de trabalho untracked (intermediárias, não versionadas): `data/pvr/`, `data/pvr-work/`,
  `data/texture-cache/`, `data/texture-test/`, `data/preprocessed/`.

### Backup automático do VPK

Confirmado em `scripts/build-butterscotch-probe.ps1`: a cada build bem-sucedido, o script copia
`src/` e `scripts/` inteiros para `artifacts/Backups/v0.68-<seq>_<timestamp>/`, com numeração
sequencial automática por versão (`v0.68-1_...`, `v0.68-2_...`, etc.). Além disso, existe na raiz
do repositório uma pasta `backups/` (não gitignored, untracked) com pelo menos um snapshot manual
mais antigo (`backups/src_backup_v068_20260803-234000/`) contendo uma cópia de `src/legacy/` e do
`ButterscotchPreprocessor-main` — aparentemente um backup manual feito por fora do script, não pelo
próprio script (que grava em `artifacts/Backups/`, não em `backups/` na raiz). Vale confirmar com o
usuário se `backups/` na raiz deve ser adicionado ao `.gitignore` ou removido do working tree (tem
centenas de arquivos, incluindo banners de outros jogos do YoYo Loader).

## Estado do cache/atlas de textura (v0.65–v0.68)

Verificado diretamente em `src/butterscotch/src/gl/gl_renderer.c` (working tree atual, não staged):
a otimização de textura descrita nas releases v0.65–v0.67 do GitHub **está presente e parece
intacta**, com uma melhoria adicional:

- Cache de textura com raiz `ux0:data/deltarune/deltarunevita/texture-cache` e limite
  (`VITA_TEXTURE_CACHE_LIMIT`, 104 MiB no código atual — ver também overrides por capítulo em
  `docs/changelog.md`/`gl_legacy_renderer.c` para o Capítulo 5, 64 MiB).
- Atlas 2048×2048 (`vitaGpuAtlasSize`) upado como RGBA4444 quando não é UI/fonte (função
  `largeAtlas`/`preserveOriginal`), cortando pela metade a VRAM residente e o tamanho de transferência
  em comparação a RGBA8888 (comentários no código citam explicitamente "cuts atlas residency and
  transfer size in half").
- UI/fontes continuam em RGBA8888 (`upload_rgba8888_ui`) para preservar qualidade de texto/ícones.
- `git diff HEAD` em `gl_renderer.c` mostra apenas 17 inserções / 9 remoções, sendo a mudança
  principal a substituição de uma captura de framebuffer via `glReadPixels` + `glTexImage2D` (com
  round-trip para a CPU) por `glCopyTexImage2D` direto (sem round-trip) — uma otimização adicional,
  não uma regressão.

**Conclusão**: com base na leitura direta do código, a otimização de texture cache/atlas NÃO parece
ter regredido. Se a suspeita do usuário de regressão pré-v0.64 persistir, ela provavelmente está em
outro subsistema (ver seção abaixo).

## Estado de v0.68 / possível regressão (Gemini)

O usuário relatou que outra IA (Gemini) pode ter revertido correções feitas anteriormente por outra
IA (GPT "Sol"), regredindo parte do código para um estado anterior à v0.64. Investigação objetiva
via `git diff HEAD` nos arquivos modificados no working tree (não staged):

- `git diff HEAD --stat` sobre `src/` mostra **2784 inserções / 460 remoções** em 41 arquivos — a
  imensa maioria das mudanças é aditiva (net-positive), não remoção de código. Os arquivos com maior
  volume de mudança são `vita_settings.c` (+567/-135), `playable_main.c` (+533/-64), `runner.c`
  (+357/-1), `vita_borders.c` (+298/-44) — todos com muito mais inserção que remoção, o que é
  consistente com desenvolvimento incremental de features, não com um revert em massa.
- O trecho da correção documentada em `docs/changelog.md` para v0.68 (crash ao carregar save no
  Capítulo 5, `room_town_south`, ajustes de `vitaTrimMiB`/`ram_reserve`/`vitaTextureCacheLimit`) **não
  foi verificado linha a linha contra o código atual** neste levantamento — recomenda-se, antes do
  próximo build de teste no Vita real, conferir especificamente se os valores citados no changelog
  (`ram_reserve` 36 MB no Capítulo 5, `vitaTextureCacheLimit` 64 MiB, `vitaTrimMiB` 0/36 MiB) batem
  com o código presente em `playable_main.c`, `gl_legacy_renderer.c` e `runner.c`, já que esses são
  justamente os arquivos com maior volume de diff e maior risco de terem sido tocados por engano.
- Não foram encontrados, nos diffs revisados (`gl_renderer.c`, `image_decoder.c`, `gl_common.c`),
  padrões óbvios de regressão (grandes blocos de código removidos sem substituição equivalente,
  reintrodução de TODOs, lógica simplificada de volta a uma versão anterior). O padrão observado é
  de adição de funcionalidade (stubs `#ifdef __vita__`/`#else` para build desktop, otimização de
  `glCopyTexImage2D`, novo inspetor de objetos/colisão v0.68).
- **Alerta para não repetir o erro**: como o CLAUDE.md e os docs foram deletados/alterados no
  working tree antes desta sessão, e o histórico de commits recentes (`d08c391`, `523b256`,
  `df5aa31`) é sobre UI (fonte de pixel, painel preto), não sobre a lógica de gameplay/renderização,
  **a checagem mais confiável de regressão real é comparar o binário/comportamento no hardware real
  contra os saves/pontos de teste que motivaram os fixes de v0.64–v0.68** (crash no Capítulo 5,
  otimização de atlas), não apenas a leitura estática do diff. Qualquer IA que for reverter algo
  neste repositório deve, no mínimo, ler `docs/changelog.md` da versão correspondente antes de tocar
  em `runner.c`, `playable_main.c`, `gl_renderer.c`/`gl_legacy_renderer.c` ou `vita_settings.c`.

## Localização dos patchers

Os patchers de distribuição (para quem já possui o jogo original e quer aplicar o port) ficam em
`artifacts/Patcher/Build_Patch/src/`: `deltarune_vita_patcher.py`, `build_patch_data.py`,
`DeltaruneVitaPatcher.spec` (PyInstaller). Esse diretório está fora do branch `main` e é gitignored —
não confundir com o código-fonte do jogo em si.

## Temporary and research files

Do not place investigation dumps, generated snippets, one-off exports or temporary AI working files in the repository root.

- `scratch/`: temporary work, quick tests, intermediate outputs and files removed from the active tree but kept for possible recovery.
- `research/`: investigation material, dumps, comparisons, exports and technical analysis that may be useful again.
- Never permanently delete project files during automated maintenance. Move replaced, obsolete or temporary files to `scratch/` or `research/` instead.
- Preserve context when moving files. If a name may collide, use a descriptive subdirectory or timestamp.
- Do not use destructive removal commands (`Remove-Item`, `del`, `rm`, etc.) on project files unless the user explicitly asks for permanent deletion.
- Existing backups under `artifacts/Backups/` are recoverable material and must not be permanently removed by routine automation.

Both `scratch/` and `research/` are intentionally ignored by Git. Promote a file out of them only when it becomes an actual project asset, source file, script or documentation artifact.
